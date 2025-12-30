// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/LPPNaniteChunkedDynamicMeshProxy.h"

#include "EngineModule.h"

FLPPNaniteChunkedDynamicMeshProxy::FLPPNaniteChunkedDynamicMeshProxy ( const ULPPDynamicMesh* Component ) : FSceneProxyBase ( Component )
{
	check ( IsValid(Component) )

	bVerifyUsedMaterials = true;

	bHasDeformableMesh                                  = false;
	bStaticElementsAlwaysUseProxyPrimitiveUniformBuffer = true;
	bSupportsSortedTriangles                            = true;

	bVFRequiresPrimitiveUniformBuffer = false;

	MeshRenderData = Component->GetRenderData ( );

	Resources = MeshRenderData->NaniteResourcesPtr.Get ( ); // Warning this is not good will cause nullptr if clear mesh

	MaterialMaxIndex = FMath::Max ( Component->GetNumMaterials ( ) - 1 , 0 );

	MaterialSections.SetNum ( MaterialMaxIndex + 1 );

	for ( int32 MaterialIndex = 0 ; MaterialIndex <= MaterialMaxIndex ; ++MaterialIndex )
	{
		UMaterialInterface* MaterialInterface = Component->GetMaterial ( MaterialIndex );

		// TODO: PROG_RASTER (Implement programmable raster support)
		const bool bInvalidMaterial = !MaterialInterface || !IsOpaqueOrMaskedBlendMode ( *MaterialInterface ) || MaterialInterface->GetShadingModels ( ).HasShadingModel ( MSM_SingleLayerWater );

		if ( bInvalidMaterial )
		{
			// force default material 
			MaterialInterface = UMaterial::GetDefaultMaterial ( MD_Surface );
		}

		// Should never be null here
		check ( MaterialInterface != nullptr );

		// Should always be opaque blend mode here.
		check ( IsOpaqueOrMaskedBlendMode(*MaterialInterface) );

		MaterialSections [ MaterialIndex ].ShadingMaterialProxy = MaterialInterface->GetRenderProxy ( );
		MaterialSections [ MaterialIndex ].RasterMaterialProxy  = MaterialInterface->GetRenderProxy ( ); // TODO: PROG_RASTER (Implement programmable raster support)
		MaterialSections [ MaterialIndex ].MaterialIndex        = MaterialIndex;
		MaterialSections [ MaterialIndex ].bCastShadow          = true;
	}

	OnMaterialsUpdated ( );

	MeshBounds = static_cast < FBox > ( MeshRenderData->LocalBounds );

	if ( MeshRenderData->DistanceFieldPtr.IsValid ( ) )
	{
		DistanceFieldPtr = MeshRenderData->DistanceFieldPtr;

		DFBias                               = Component->GetDistanceFieldSelfShadowBias ( );
		bSupportsDistanceFieldRepresentation = true;

		bAffectDistanceFieldLighting = Component->bAffectDistanceFieldLighting;
	}

	if ( MeshRenderData->LumenCardData.IsValid ( ) )
	{
		LumenCardData = MeshRenderData->LumenCardData;

		UpdateVisibleInLumenScene ( );
	}
};

void FLPPNaniteChunkedDynamicMeshProxy::CreateRenderThreadResources ( FRHICommandListBase& RHICmdList )
{
	check ( Resources->RuntimeResourceID != INDEX_NONE && Resources->HierarchyOffset != INDEX_NONE );

	//#if RHI_RAYTRACING
	//	if ( IsRayTracingAllowed ( ) )
	//	{
	//		// copy RayTracingGeometryGroupHandle from FStaticMeshRenderData since UStaticMesh can be released before the proxy is destroyed
	//		RayTracingGeometryGroupHandle = RenderData->RayTracingGeometryGroupHandle;
	//	}
	//
	//	if ( IsRayTracingEnabled ( ) && bDynamicRayTracingGeometry )
	//	{
	//		CreateDynamicRayTracingGeometries ( RHICmdList );
	//	}
	//#endif
}

void FLPPNaniteChunkedDynamicMeshProxy::OnEvaluateWorldPositionOffsetChanged_RenderThread ( )
{
	bHasVertexProgrammableRaster = false;
	for ( FMaterialSection& MaterialSection : MaterialSections )
	{
		const bool bVertexProgrammable = MaterialSection.IsVertexProgrammableRaster ( bEvaluateWorldPositionOffset );
		const bool bPixelProgrammable  = MaterialSection.IsPixelProgrammableRaster ( );
		if ( bVertexProgrammable || bPixelProgrammable )
		{
			MaterialSection.RasterMaterialProxy = MaterialSection.ShadingMaterialProxy;
			bHasVertexProgrammableRaster        |= bVertexProgrammable;
		}
		else
		{
			MaterialSection.ResetToDefaultMaterial ( false , true );
		}
	}

	GetRendererModule ( ).RequestStaticMeshUpdate ( GetPrimitiveSceneInfo ( ) );
}

SIZE_T FLPPNaniteChunkedDynamicMeshProxy::GetTypeHash ( ) const
{
	static size_t UniquePointer;
	return reinterpret_cast < size_t > ( &UniquePointer );
}

FPrimitiveViewRelevance FLPPNaniteChunkedDynamicMeshProxy::GetViewRelevance ( const FSceneView* View ) const
{
	LLM_SCOPE_BYTAG ( Nanite );

#if WITH_EDITOR
	const bool bOptimizedRelevance = false;
#else
	const bool bOptimizedRelevance = true;
#endif

	FPrimitiveViewRelevance Result;
	Result.bDrawRelevance        = IsShown ( View ) && !!View->Family->EngineShowFlags.NaniteMeshes;
	Result.bShadowRelevance      = IsShadowCast ( View );
	Result.bRenderCustomDepth    = Nanite::GetSupportsCustomDepthRendering ( ) && ShouldRenderCustomDepth ( );
	Result.bUsesLightingChannels = GetLightingChannelMask ( ) != GetDefaultLightingChannelMask ( );

	// Always render the Nanite mesh data with static relevance.
	Result.bStaticRelevance = true;

	// Should always be covered by constructor of Nanite scene proxy.
	Result.bRenderInMainPass = true;

	if ( bOptimizedRelevance ) // No dynamic relevance if optimized.
	{
		CombinedMaterialRelevance.SetPrimitiveViewRelevance ( Result );
		Result.bVelocityRelevance = DrawsVelocity ( );
	}
	else
	{
#if WITH_EDITOR
		//only check these in the editor
		Result.bEditorVisualizeLevelInstanceRelevance = IsEditingLevelInstanceChild ( );
		Result.bEditorStaticSelectionRelevance        = ( WantsEditorEffects ( ) || IsSelected ( ) || IsHovered ( ) );
#endif

#if NANITE_ENABLE_DEBUG_RENDERING
		bool       bDrawSimpleCollision = false , bDrawComplexCollision = false;
		const bool bInCollisionView     = IsCollisionView ( View->Family->EngineShowFlags , bDrawSimpleCollision , bDrawComplexCollision );
#else
		bool bInCollisionView = false;
#endif

		// Set dynamic relevance for overlays like collision and bounds.
		bool bSetDynamicRelevance = false;
#if !(UE_BUILD_SHIPPING) || WITH_EDITOR
		bSetDynamicRelevance |= (
			// Nanite doesn't respect rich view enabling dynamic relevancy.
			//IsRichView(*View->Family) ||
			View->Family->EngineShowFlags.Collision ||
			bInCollisionView ||
			View->Family->EngineShowFlags.Bounds ||
			View->Family->EngineShowFlags.VisualizeInstanceUpdates
		);
#endif
#if NANITE_ENABLE_DEBUG_RENDERING
		bSetDynamicRelevance |= bDrawMeshCollisionIfComplex || bDrawMeshCollisionIfSimple;
#endif

		if ( bSetDynamicRelevance )
		{
			Result.bDynamicRelevance = true;

#if NANITE_ENABLE_DEBUG_RENDERING
			// If we want to draw collision, needs to make sure we are considered relevant even if hidden
			if ( View->Family->EngineShowFlags.Collision || bInCollisionView )
			{
				Result.bDrawRelevance = true;
			}
#endif
		}

		if ( !View->Family->EngineShowFlags.Materials
#if NANITE_ENABLE_DEBUG_RENDERING
		     || bInCollisionView
#endif
		)
		{
			Result.bOpaque = true;
		}

		CombinedMaterialRelevance.SetPrimitiveViewRelevance ( Result );
		Result.bVelocityRelevance = Result.bOpaque && Result.bRenderInMainPass && DrawsVelocity ( );
	}

	return Result;
}

//#if RHI_RAYTRACING || NANITE_ENABLE_DEBUG_RENDERING
//
//// Loosely copied from FStaticMeshSceneProxy::FLODInfo::FLODInfo and modified for Nanite fallback
//// TODO: Refactor all this to share common code with Nanite and regular SM scene proxy
//FLPPNaniteChunkedDynamicMeshProxy::FFallbackLODInfo::FFallbackLODInfo (
//	const FStaticMeshSceneProxyDesc&  InProxyDesc ,
//	const FStaticMeshVertexBuffers&   InVertexBuffers ,
//	const FStaticMeshSectionArray&    InSections ,
//	const FStaticMeshVertexFactories& InVertexFactories ,
//	int32                             LODIndex ,
//	int32                             InClampedMinLOD
//	)
//{
//	if ( LODIndex < InProxyDesc.LODData.Num ( ) && LODIndex >= InClampedMinLOD )
//	{
//		const FStaticMeshComponentLODInfo& ComponentLODInfo = InProxyDesc.LODData [ LODIndex ];
//
//		// Initialize this LOD's overridden vertex colors, if it has any
//		if ( ComponentLODInfo.OverrideVertexColors )
//		{
//			bool bBroken = false;
//			for ( int32 SectionIndex = 0 ; SectionIndex < InSections.Num ( ) ; SectionIndex++ )
//			{
//				const FStaticMeshSection& Section = InSections [ SectionIndex ];
//				if ( Section.MaxVertexIndex >= ComponentLODInfo.OverrideVertexColors->GetNumVertices ( ) )
//				{
//					bBroken = true;
//					break;
//				}
//			}
//			if ( !bBroken )
//			{
//				// the instance should point to the loaded data to avoid copy and memory waste
//				OverrideColorVertexBuffer = ComponentLODInfo.OverrideVertexColors;
//				check ( OverrideColorVertexBuffer->GetStride() == sizeof(FColor) ); //assumed when we set up the stream
//
//				if ( RHISupportsManualVertexFetch ( GMaxRHIShaderPlatform ) || IsStaticLightingAllowed ( ) )
//				{
//					TUniformBufferRef < FLocalVertexFactoryUniformShaderParameters >* UniformBufferPtr = &OverrideColorVFUniformBuffer;
//					const FLocalVertexFactory*                                        LocalVF          = &InVertexFactories.VertexFactoryOverrideColorVertexBuffer;
//					FColorVertexBuffer*                                               VertexBuffer     = OverrideColorVertexBuffer;
//
//					//temp measure to identify nullptr crashes deep in the renderer
//					FString ComponentPathName = InProxyDesc.GetPathName ( );
//					checkf ( InVertexBuffers.PositionVertexBuffer.GetNumVertices() > 0 , TEXT("LOD: %i of PathName: %s has an empty position stream.") , LODIndex , *ComponentPathName );
//
//					ENQUEUE_RENDER_COMMAND ( FLocalVertexFactoryCopyData ) (
//					                                                        [UniformBufferPtr, LocalVF, LODIndex, VertexBuffer, ComponentPathName] ( FRHICommandListBase& )
//					                                                        {
//						                                                        checkf ( LocalVF->GetTangentsSRV() , TEXT("LOD: %i of PathName: %s has a null tangents srv.") , LODIndex , *ComponentPathName );
//						                                                        checkf ( LocalVF->GetTextureCoordinatesSRV() , TEXT("LOD: %i of PathName: %s has a null texcoord srv.") , LODIndex , *ComponentPathName );
//						                                                        *UniformBufferPtr = CreateLocalVFUniformBuffer ( LocalVF , LODIndex , VertexBuffer , 0 , 0 );
//					                                                        } );
//				}
//			}
//		}
//	}
//
//	// Gather the materials applied to the LOD.
//	Sections.Empty ( InSections.Num ( ) );
//	for ( int32 SectionIndex = 0 ; SectionIndex < InSections.Num ( ) ; SectionIndex++ )
//	{
//		const FStaticMeshSection& Section = InSections [ SectionIndex ];
//		FSectionInfo              SectionInfo;
//
//		// Determine the material applied to this element of the LOD.
//		UMaterialInterface* Material = InProxyDesc.GetMaterial ( Section.MaterialIndex , /*bDoingNaniteMaterialAudit*/ false , /*bIgnoreNaniteOverrideMaterials*/ true );
//#if WITH_EDITORONLY_DATA
//		SectionInfo.MaterialIndex = Section.MaterialIndex;
//#endif
//
//		if ( Material == nullptr )
//		{
//			Material = UMaterial::GetDefaultMaterial ( MD_Surface );
//		}
//
//		SectionInfo.MaterialProxy = Material->GetRenderProxy ( );
//
//		// Per-section selection for the editor.
//#if WITH_EDITORONLY_DATA
//		if ( GIsEditor )
//		{
//			if ( InProxyDesc.SelectedEditorMaterial >= 0 )
//			{
//				SectionInfo.bSelected = ( InProxyDesc.SelectedEditorMaterial == Section.MaterialIndex );
//			}
//			else
//			{
//				SectionInfo.bSelected = ( InProxyDesc.SelectedEditorSection == SectionIndex );
//			}
//		}
//#endif
//
//		// Store the element info.
//		Sections.Add ( SectionInfo );
//	}
//}
//
//#endif

void FLPPNaniteChunkedDynamicMeshProxy::DrawStaticElements ( FStaticPrimitiveDrawInterface* PDI )
{
	const FLightCacheInterface* LCI = nullptr; // No Static Light Support
	DrawStaticElementsInternal ( PDI , LCI );
}

// Loosely copied from FStaticMeshSceneProxy::GetDynamicMeshElements and modified for Nanite fallback
// TODO: Refactor all this to share common code with Nanite and regular SM scene proxy
//void FLPPNaniteChunkedDynamicMeshProxy::GetDynamicMeshElements ( const TArray < const FSceneView* >& Views , const FSceneViewFamily& ViewFamily , uint32 VisibilityMap , FMeshElementCollector& Collector ) const
//{
//	// Nanite only has dynamic relevance in the editor for certain debug modes
//#if WITH_EDITOR
//	LLM_SCOPE_BYTAG ( Nanite );
//	QUICK_SCOPE_CYCLE_COUNTER ( STAT_NaniteSceneProxy_GetMeshElements );
//
//	const bool              bProxyIsSelected = WantsEditorEffects ( ) || IsSelected ( );
//	const FEngineShowFlags& EngineShowFlags  = ViewFamily.EngineShowFlags;
//
//	bool       bDrawSimpleCollision = false , bDrawComplexCollision = false;
//	const bool bInCollisionView     = IsCollisionView ( EngineShowFlags , bDrawSimpleCollision , bDrawComplexCollision );
//
//#if NANITE_ENABLE_DEBUG_RENDERING
//	// Collision and bounds drawing
//	FColor SimpleCollisionColor  = FColor ( 157 , 149 , 223 , 255 );
//	FColor ComplexCollisionColor = FColor ( 0 , 255 , 255 , 255 );
//
//
//	// Make material for drawing complex collision mesh
//	UMaterial*   ComplexCollisionMaterial = UMaterial::GetDefaultMaterial ( MD_Surface );
//	FLinearColor DrawCollisionColor       = GetWireframeColor ( );
//
//	// Collision view modes draw collision mesh as solid
//	if ( bInCollisionView )
//	{
//		ComplexCollisionMaterial = GEngine->ShadedLevelColorationUnlitMaterial;
//	}
//	// Wireframe, choose color based on complex or simple
//	else
//	{
//		ComplexCollisionMaterial = GEngine->WireframeMaterial;
//		DrawCollisionColor       = ( CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple ) ? SimpleCollisionColor : ComplexCollisionColor;
//	}
//
//	// Create colored proxy
//	FColoredMaterialRenderProxy* ComplexCollisionMaterialInstance = new FColoredMaterialRenderProxy ( ComplexCollisionMaterial->GetRenderProxy ( ) , DrawCollisionColor );
//	Collector.RegisterOneFrameMaterialProxy ( ComplexCollisionMaterialInstance );
//
//
//	// Make a material for drawing simple solid collision stuff
//	auto SimpleCollisionMaterialInstance = new FColoredMaterialRenderProxy (
//	                                                                        GEngine->ShadedLevelColorationUnlitMaterial->GetRenderProxy ( ) ,
//	                                                                        GetWireframeColor ( )
//	                                                                       );
//
//	Collector.RegisterOneFrameMaterialProxy ( SimpleCollisionMaterialInstance );
//
//	for ( int32 ViewIndex = 0 ; ViewIndex < Views.Num ( ) ; ViewIndex++ )
//	{
//		if ( VisibilityMap & ( 1 << ViewIndex ) )
//		{
//			if ( AllowDebugViewmodes ( ) )
//			{
//				// Should we draw the mesh wireframe to indicate we are using the mesh as collision
//				bool bDrawComplexWireframeCollision = ( EngineShowFlags.Collision && IsCollisionEnabled ( ) && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple );
//
//				// Requested drawing complex in wireframe, but check that we are not using simple as complex
//				bDrawComplexWireframeCollision |= ( bDrawMeshCollisionIfComplex && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseSimpleAsComplex );
//
//				// Requested drawing simple in wireframe, and we are using complex as simple
//				bDrawComplexWireframeCollision |= ( bDrawMeshCollisionIfSimple && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple );
//
//				// If drawing complex collision as solid or wireframe
//				if ( bDrawComplexWireframeCollision || ( bInCollisionView && bDrawComplexCollision ) )
//				{
//					// If we have at least one valid LOD to draw
//					if ( RenderData->LODResources.Num ( ) > 0 )
//					{
//						// Get LOD used for collision
//						int32                          DrawLOD  = FMath::Clamp ( LODForCollision , 0 , RenderData->LODResources.Num ( ) - 1 );
//						const FStaticMeshLODResources& LODModel = RenderData->LODResources [ DrawLOD ];
//
//						// Iterate over sections of that LOD
//						for ( int32 SectionIndex = 0 ; SectionIndex < LODModel.Sections.Num ( ) ; SectionIndex++ )
//						{
//							// If this section has collision enabled
//							if ( LODModel.Sections [ SectionIndex ].bEnableCollision )
//							{
//#if WITH_EDITOR
//								// See if we are selected
//								const bool bSectionIsSelected = FallbackLODs [ DrawLOD ].Sections [ SectionIndex ].bSelected;
//#else
//								const bool bSectionIsSelected = false;
//#endif
//
//								// Iterate over batches
//								const int32 NumMeshBatches = 1; // TODO: GetNumMeshBatches()
//								for ( int32 BatchIndex = 0 ; BatchIndex < NumMeshBatches ; BatchIndex++ )
//								{
//									FMeshBatch& CollisionElement = Collector.AllocateMesh ( );
//									if ( GetCollisionMeshElement ( DrawLOD , BatchIndex , SectionIndex , SDPG_World , ComplexCollisionMaterialInstance , CollisionElement ) )
//									{
//										Collector.AddMesh ( ViewIndex , CollisionElement );
//										INC_DWORD_STAT_BY ( STAT_StaticMeshTriangles , CollisionElement.GetNumPrimitives() );
//									}
//								}
//							}
//						}
//					}
//				}
//			}
//
//			// Draw simple collision as wireframe if 'show collision', collision is enabled, and we are not using the complex as the simple
//			const bool bDrawSimpleWireframeCollision = ( EngineShowFlags.Collision && IsCollisionEnabled ( ) && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseComplexAsSimple );
//
//			{
//				FMatrix InstanceToWorld = GetLocalToWorld ( );
//
//				if ( ( bDrawSimpleCollision || bDrawSimpleWireframeCollision ) && BodySetup )
//				{
//					if ( FMath::Abs ( InstanceToWorld.Determinant ( ) ) < UE_SMALL_NUMBER )
//					{
//						// Catch this here or otherwise GeomTransform below will assert
//						// This spams so commented out
//						//UE_LOG(LogNanite, Log, TEXT("Zero scaling not supported (%s)"), *StaticMesh->GetPathName());
//					}
//					else
//					{
//						const bool bDrawSolid = !bDrawSimpleWireframeCollision;
//
//						if ( AllowDebugViewmodes ( ) && bDrawSolid )
//						{
//							FTransform GeomTransform ( InstanceToWorld );
//							BodySetup->AggGeom.GetAggGeom ( GeomTransform , GetWireframeColor ( ).ToFColor ( true ) , SimpleCollisionMaterialInstance , false , true , AlwaysHasVelocity ( ) , ViewIndex , Collector );
//						}
//						// wireframe
//						else
//						{
//							FTransform GeomTransform ( InstanceToWorld );
//							BodySetup->AggGeom.GetAggGeom ( GeomTransform , GetSelectionColor ( SimpleCollisionColor , bProxyIsSelected , IsHovered ( ) ).ToFColor ( true ) , nullptr , ( Owner == nullptr ) , false , AlwaysHasVelocity ( ) , ViewIndex , Collector );
//						}
//					}
//				}
//
//				if ( EngineShowFlags.MassProperties && DebugMassData.Num ( ) > 0 )
//				{
//					DebugMassData [ 0 ].DrawDebugMass ( Collector.GetPDI ( ViewIndex ) , FTransform ( InstanceToWorld ) );
//				}
//
//				if ( EngineShowFlags.StaticMeshes )
//				{
//					RenderBounds ( Collector.GetPDI ( ViewIndex ) , EngineShowFlags , GetBounds ( ) , !Owner || IsSelected ( ) );
//				}
//			}
//		}
//	}
//#endif // NANITE_ENABLE_DEBUG_RENDERING
//#endif // WITH_EDITOR
//}
//
void FLPPNaniteChunkedDynamicMeshProxy::GetPreSkinnedLocalBounds ( FBoxSphereBounds& OutBounds ) const
{
	OutBounds = MeshBounds;
}

//#if NANITE_ENABLE_DEBUG_RENDERING
//
//// Loosely copied from FStaticMeshSceneProxy::GetCollisionMeshElement and modified for Nanite fallback
//// TODO: Refactor all this to share common code with Nanite and regular SM scene proxy
//bool FLPPNaniteChunkedDynamicMeshProxy::GetCollisionMeshElement (
//	int32                       LODIndex ,
//	int32                       BatchIndex ,
//	int32                       SectionIndex ,
//	uint8                       InDepthPriorityGroup ,
//	const FMaterialRenderProxy* RenderProxy ,
//	FMeshBatch&                 OutMeshBatch ) const
//{
//	const FStaticMeshLODResources&    LOD     = RenderData->LODResources [ LODIndex ];
//	const FStaticMeshVertexFactories& VFs     = RenderData->LODVertexFactories [ LODIndex ];
//	const FStaticMeshSection&         Section = LOD.Sections [ SectionIndex ];
//
//	if ( Section.NumTriangles == 0 )
//	{
//		return false;
//	}
//
//	const ::FVertexFactory* VertexFactory = nullptr;
//
//	const FFallbackLODInfo& ProxyLODInfo = FallbackLODs [ LODIndex ];
//
//	const bool bWireframe             = false;
//	const bool bUseReversedIndices    = false;
//	const bool bDitheredLODTransition = false;
//
//	SetMeshElementGeometrySource ( Section , ProxyLODInfo.Sections [ SectionIndex ] , LOD.IndexBuffer , LOD.AdditionalIndexBuffers , VertexFactory , bWireframe , bUseReversedIndices , OutMeshBatch );
//
//	FMeshBatchElement& OutMeshBatchElement = OutMeshBatch.Elements [ 0 ];
//
//	if ( ProxyLODInfo.OverrideColorVertexBuffer )
//	{
//		VertexFactory = &VFs.VertexFactoryOverrideColorVertexBuffer;
//
//		OutMeshBatchElement.VertexFactoryUserData = ProxyLODInfo.OverrideColorVFUniformBuffer.GetReference ( );
//	}
//	else
//	{
//		VertexFactory = &VFs.VertexFactory;
//
//		OutMeshBatchElement.VertexFactoryUserData = VFs.VertexFactory.GetUniformBuffer ( );
//	}
//
//	if ( OutMeshBatchElement.NumPrimitives > 0 )
//	{
//		OutMeshBatch.LODIndex                     = LODIndex;
//		OutMeshBatch.VisualizeLODIndex            = LODIndex;
//		OutMeshBatch.VisualizeHLODIndex           = 0; // HierarchicalLODIndex;
//		OutMeshBatch.ReverseCulling               = IsReversedCullingNeeded ( bUseReversedIndices );
//		OutMeshBatch.CastShadow                   = false;
//		OutMeshBatch.DepthPriorityGroup           = ( ESceneDepthPriorityGroup ) InDepthPriorityGroup;
//		OutMeshBatch.LCI                          = nullptr; // No Static Light Support;
//		OutMeshBatch.VertexFactory                = VertexFactory;
//		OutMeshBatch.MaterialRenderProxy          = RenderProxy;
//		OutMeshBatchElement.MinVertexIndex        = Section.MinVertexIndex;
//		OutMeshBatchElement.MaxVertexIndex        = Section.MaxVertexIndex;
//		OutMeshBatchElement.VisualizeElementIndex = SectionIndex;
//
//		if ( ForcedLodModel > 0 )
//		{
//			OutMeshBatch.bDitheredLODTransition = false;
//
//			OutMeshBatchElement.MaxScreenSize = 0.0f;
//			OutMeshBatchElement.MinScreenSize = -1.0f;
//		}
//		else
//		{
//			OutMeshBatch.bDitheredLODTransition = bDitheredLODTransition;
//
//			OutMeshBatchElement.MaxScreenSize = RenderData->ScreenSize [ LODIndex ].GetValue ( );
//			OutMeshBatchElement.MinScreenSize = 0.0f;
//			if ( LODIndex < MAX_STATIC_MESH_LODS - 1 )
//			{
//				OutMeshBatchElement.MinScreenSize = RenderData->ScreenSize [ LODIndex + 1 ].GetValue ( );
//			}
//		}
//
//		return true;
//	}
//	else
//	{
//		return false;
//	}
//}
//
//#endif

//void FLPPNaniteChunkedDynamicMeshProxy::SetEvaluateWorldPositionOffsetInRayTracing ( FRHICommandListBase& RHICmdList , bool bEvaluate )
//{
//#if RHI_RAYTRACING
//	if ( !bSupportRayTracing )
//	{
//		return;
//	}
//
//	const int32 RayTracingClampedMinLOD = RenderData->RayTracingProxy->bUsingRenderingLODs ? ClampedMinLOD : 0;
//
//	bHasRayTracingRepresentation = RenderData->RayTracingProxy->LODs [ RayTracingClampedMinLOD ].VertexBuffers->StaticMeshVertexBuffer.GetNumVertices ( ) > 0;
//
//	const bool bWantsRayTracingWPO = bEvaluate && CombinedMaterialRelevance.bUsesWorldPositionOffset;
//
//	bool bNewDynamicRayTracingGeometry = false;
//
//	if ( !bDynamicRayTracingGeometry && bNewDynamicRayTracingGeometry )
//	{
//		bDynamicRayTracingGeometry = bNewDynamicRayTracingGeometry;
//		CreateDynamicRayTracingGeometries ( RHICmdList );
//	}
//	else if ( bDynamicRayTracingGeometry && !bNewDynamicRayTracingGeometry )
//	{
//		ReleaseDynamicRayTracingGeometries ( );
//		bDynamicRayTracingGeometry = bNewDynamicRayTracingGeometry;
//	}
//
//	GetScene ( ).UpdateCachedRayTracingState ( this );
//#endif
//}

//#if RHI_RAYTRACING
//bool FLPPNaniteChunkedDynamicMeshProxy::HasRayTracingRepresentation ( ) const
//{
//	// TODO: check CVarRayTracingNaniteProxyMeshes here instead of during GetCachedRayTracingInstance(...)
//	// would avoid unnecessarily including proxy in Lumen Scene
//	return bHasRayTracingRepresentation;
//}
//
//int32 FLPPNaniteChunkedDynamicMeshProxy::GetFirstValidRaytracingGeometryLODIndex ( ERayTracingMode RayTracingMode , bool bForDynamicUpdate ) const
//{
//	if ( RayTracingMode != ERayTracingMode::Fallback )
//	{
//		checkf ( !bForDynamicUpdate , TEXT("Nanite Ray Tracing is not compatible with dynamic BLAS update.") );
//
//		// NaniteRayTracing always uses LOD0
//		return 0;
//	}
//
//	FStaticMeshRayTracingProxyLODArray& RayTracingLODs = RenderData->RayTracingProxy->LODs;
//
//	const int32 NumLODs = RayTracingLODs.Num ( );
//
//	int32 RayTracingMinLOD  = RenderData->RayTracingProxy->bUsingRenderingLODs ? RenderData->GetCurrentFirstLODIdx ( ClampedMinLOD ) : 0;
//	int32 RayTracingLODBias = CVarRayTracingNaniteProxyMeshesLODBias.GetValueOnRenderThread ( );
//
//#if WITH_EDITOR
//	// If coarse mesh streaming mode is set to 2 then we force use the lowest LOD to visualize streamed out coarse meshes
//	if ( Nanite::FCoarseMeshStreamingManager::GetStreamingMode ( ) == 2 )
//	{
//		RayTracingMinLOD = NumLODs - 1;
//	}
//	else if ( RenderData->RayTracingProxy->PreviewLODLevel >= 0 )
//	{
//		RayTracingMinLOD  = FMath::Max ( RayTracingMinLOD , RenderData->RayTracingProxy->PreviewLODLevel );
//		RayTracingLODBias = 0;
//	}
//#endif // WITH_EDITOR
//
//	// TODO: take LOD bias into account when managing BLAS residency
//	RayTracingMinLOD = FMath::Clamp ( RayTracingMinLOD + RayTracingLODBias , RayTracingMinLOD , NumLODs - 1 );
//
//	// find the first valid RT geometry index
//	for ( int32 LODIndex = RayTracingMinLOD ; LODIndex < NumLODs ; ++LODIndex )
//	{
//		const FRayTracingGeometry& RayTracingGeometry = *RayTracingLODs [ LODIndex ].RayTracingGeometry;
//		if ( bForDynamicUpdate )
//		{
//			if ( RenderData->RayTracingProxy->bUsingRenderingLODs || RayTracingLODs [ LODIndex ].bBuffersInlined || RayTracingLODs [ LODIndex ].AreBuffersStreamedIn ( ) )
//			{
//				return LODIndex;
//			}
//		}
//		else if ( RayTracingGeometry.IsValid ( ) && !RayTracingGeometry.IsEvicted ( ) && !RayTracingGeometry.HasPendingBuildRequest ( ) )
//		{
//			return LODIndex;
//		}
//	}
//
//	return INDEX_NONE;
//}
//
//void FLPPNaniteChunkedDynamicMeshProxy::SetupFallbackRayTracingMaterials ( int32 LODIndex , TArray < FMeshBatch >& OutMaterials ) const
//{
//	const FStaticMeshRayTracingProxyLOD& LOD = RenderData->RayTracingProxy->LODs [ LODIndex ];
//	const FStaticMeshVertexFactories&    VFs = ( *RenderData->RayTracingProxy->LODVertexFactories ) [ LODIndex ];
//
//	const FFallbackLODInfo& FallbackLODInfo = RayTracingFallbackLODs [ LODIndex ];
//
//	OutMaterials.SetNum ( FallbackLODInfo.Sections.Num ( ) );
//
//	for ( int32 SectionIndex = 0 ; SectionIndex < OutMaterials.Num ( ) ; ++SectionIndex )
//	{
//		const FStaticMeshSection&             Section     = ( *LOD.Sections ) [ SectionIndex ];
//		const FFallbackLODInfo::FSectionInfo& SectionInfo = FallbackLODInfo.Sections [ SectionIndex ];
//
//		FMeshBatch&        MeshBatch        = OutMaterials [ SectionIndex ];
//		FMeshBatchElement& MeshBatchElement = MeshBatch.Elements [ 0 ];
//
//		const bool bWireframe          = false;
//		const bool bUseReversedIndices = false;
//
//		SetMeshElementGeometrySource ( Section , SectionInfo , *LOD.IndexBuffer , nullptr , &VFs.VertexFactory , bWireframe , bUseReversedIndices , MeshBatch );
//
//		MeshBatch.VertexFactory                = &VFs.VertexFactory;
//		MeshBatchElement.VertexFactoryUserData = VFs.VertexFactory.GetUniformBuffer ( );
//
//		MeshBatchElement.MinVertexIndex = Section.MinVertexIndex;
//		MeshBatchElement.MaxVertexIndex = Section.MaxVertexIndex;
//
//		MeshBatch.MaterialRenderProxy        = SectionInfo.MaterialProxy;
//		MeshBatch.bWireframe                 = bWireframe;
//		MeshBatch.SegmentIndex               = SectionIndex;
//		MeshBatch.LODIndex                   = 0;                                             // CacheRayTracingPrimitive(...) currently assumes that primitives with CacheInstances flag only cache mesh commands for one LOD
//		MeshBatch.CastRayTracedShadow        = Section.bCastShadow && CastsDynamicShadow ( ); // Relying on BuildInstanceMaskAndFlags(...) to check Material.CastsRayTracedShadows()
//		MeshBatch.bForceTwoSidedInRayTracing = ( Resources->VoxelMaterialsMask & ( 1ull << Section.MaterialIndex ) ) != 0;
//
//		MeshBatchElement.PrimitiveUniformBufferResource = &GIdentityPrimitiveUniformBuffer;
//	}
//}
//
//void FLPPNaniteChunkedDynamicMeshProxy::CreateDynamicRayTracingGeometries ( FRHICommandListBase& RHICmdList )
//{
//	check ( bDynamicRayTracingGeometry );
//	check ( DynamicRayTracingGeometries.IsEmpty() );
//
//	FStaticMeshRayTracingProxyLODArray& RayTracingLODs = RenderData->RayTracingProxy->LODs;
//
//	DynamicRayTracingGeometries.AddDefaulted ( RayTracingLODs.Num ( ) );
//
//	const int32 RayTracingMinLOD = RenderData->RayTracingProxy->bUsingRenderingLODs ? ClampedMinLOD : 0;
//
//	for ( int32 LODIndex = RayTracingMinLOD ; LODIndex < RayTracingLODs.Num ( ) ; LODIndex++ )
//	{
//		FRayTracingGeometryInitializer Initializer = RayTracingLODs [ LODIndex ].RayTracingGeometry->Initializer;
//		for ( FRayTracingGeometrySegment& Segment : Initializer.Segments )
//		{
//			Segment.VertexBuffer = nullptr;
//		}
//		Initializer.bAllowUpdate = true;
//		Initializer.bFastBuild   = true;
//		Initializer.Type         = ERayTracingGeometryInitializerType::Rendering;
//
//		DynamicRayTracingGeometries [ LODIndex ].SetInitializer ( MoveTemp ( Initializer ) );
//		DynamicRayTracingGeometries [ LODIndex ].InitResource ( RHICmdList );
//	}
//}
//
//void FLPPNaniteChunkedDynamicMeshProxy::ReleaseDynamicRayTracingGeometries ( )
//{
//	checkf ( DynamicRayTracingGeometries.IsEmpty() || bDynamicRayTracingGeometry , TEXT("Proxy shouldn't have DynamicRayTracingGeometries since bDynamicRayTracingGeometry is false.") );
//
//	for ( auto& Geometry : DynamicRayTracingGeometries )
//	{
//		Geometry.ReleaseResource ( );
//	}
//
//	DynamicRayTracingGeometries.Empty ( );
//}
//
//void FLPPNaniteChunkedDynamicMeshProxy::GetDynamicRayTracingInstances ( FRayTracingInstanceCollector& Collector )
//{
//	if ( CVarRayTracingNaniteProxyMeshes.GetValueOnRenderThread ( ) == 0 )
//	{
//		return;
//	}
//
//	return GetDynamicRayTracingInstances_Internal ( Collector , nullptr , true );
//}
//
//void FLPPNaniteChunkedDynamicMeshProxy::GetDynamicRayTracingInstances_Internal ( FRayTracingInstanceCollector& Collector , FRWBuffer* DynamicVertexBuffer , bool bUpdateRayTracingGeometry )
//{
//#if DO_CHECK
//	// TODO: Once workaround below is removed we should check bDynamicRayTracingGeometry here 
//	if ( !ensureMsgf ( IsRayTracingRelevant() && bSupportRayTracing && bHasRayTracingRepresentation ,
//	                   TEXT("Nanite::FLPPNaniteChunkedDynamicMeshProxy::GetDynamicRayTracingInstances(...) should only be called for proxies using dynamic raytracing geometry. ")
//	                   TEXT("Ray tracing primitive gathering code may be wrong.") ) )
//	{
//		return;
//	}
//#endif
//
//	// Workaround: SetEvaluateWorldPositionOffsetInRayTracing(...) calls UpdateCachedRayTracingState(...)
//	// however the update only happens after gathering relevant ray tracing primitives
//	// so ERayTracingPrimitiveFlags::Dynamic is set for one frame after the WPO evaluation is disabled.
//	if ( !bDynamicRayTracingGeometry )
//	{
//		return;
//	}
//
//	checkf ( !DynamicRayTracingGeometries.IsEmpty() , TEXT("Proxy should have entries in DynamicRayTracingGeometries when using the GetDynamicRayTracingInstances() code path.") );
//
//	TConstArrayView < const FSceneView* > Views         = Collector.GetViews ( );
//	const uint32                          VisibilityMap = Collector.GetVisibilityMap ( );
//
//	// RT geometry will be generated based on first active view and then reused for all other views
//	// TODO: Expose a way for developers to control whether to reuse RT geometry or create one per-view
//	const int32 FirstActiveViewIndex = FMath::CountTrailingZeros ( VisibilityMap );
//	checkf ( Views.IsValidIndex(FirstActiveViewIndex) , TEXT("There should be at least one active view when calling GetDynamicRayTracingInstances(...).") );
//
//	// TODO: Support ERayTracingMode::StreamOut. Currently always uses fallback for splines or when WPO is enabled
//
//	bool bUseDynamicGeometry = bSplineMesh;
//
//	for ( int32 ViewIndex = 0 ; ViewIndex < Views.Num ( ) ; ViewIndex++ )
//	{
//		if ( ( VisibilityMap & ( 1 << ViewIndex ) ) == 0 )
//		{
//			continue;
//		}
//
//		const FSceneView& SceneView  = *Views [ ViewIndex ];
//		const FVector     ViewCenter = SceneView.ViewMatrices.GetViewOrigin ( );
//
//		bUseDynamicGeometry |= ShouldStaticMeshEvaluateWPOInRayTracing ( ViewCenter , GetBounds ( ) );
//	}
//
//	if ( bUseDynamicGeometry && !RenderData->RayTracingProxy->bUsingRenderingLODs )
//	{
//		// when using WPO, need to mark the geometry group as referenced since VB/IB need to be streamed-in 
//		// TODO: Support streaming only buffers when using dynamic geometry
//		Collector.AddReferencedGeometryGroupForDynamicUpdate ( RenderData->RayTracingGeometryGroupHandle );
//	}
//
//	int32 ValidLODIndex = INDEX_NONE;
//
//	// find the first valid RT geometry index
//
//	if ( bUseDynamicGeometry )
//	{
//		ValidLODIndex = GetFirstValidRaytracingGeometryLODIndex ( ERayTracingMode::Fallback , /*bForDynamicUpdate*/ true );
//
//		if ( ValidLODIndex == INDEX_NONE )
//		{
//			// if none of the LODs have buffers ready for dynamic BLAS update, fallback to static BLAS
//			bUseDynamicGeometry = false;
//		}
//	}
//
//	if ( !bUseDynamicGeometry )
//	{
//		ValidLODIndex = GetFirstValidRaytracingGeometryLODIndex ( ERayTracingMode::Fallback , /*bForDynamicUpdate*/ false );
//	}
//
//	if ( ValidLODIndex == INDEX_NONE )
//	{
//		// if none of the LODs have the data necessary for ray tracing, skip adding instances
//		// referenced geometries were still added to Collector so ray tracing geometry manager will try to stream-in necessary data
//		return;
//	}
//
//	FStaticMeshRayTracingProxyLOD& RayTracingLOD = RenderData->RayTracingProxy->LODs [ ValidLODIndex ];
//
//	FRayTracingGeometry* DynamicRayTracingGeometry = nullptr;
//
//	if ( bUseDynamicGeometry )
//	{
//		if ( !ensure ( DynamicRayTracingGeometries.IsValidIndex(ValidLODIndex) ) )
//		{
//			return;
//		}
//
//		DynamicRayTracingGeometry = &DynamicRayTracingGeometries [ ValidLODIndex ];
//
//		const bool bNeedsUpdate = bUpdateRayTracingGeometry
//		                          || ( DynamicRayTracingGeometry->DynamicGeometrySharedBufferGenerationID != FRayTracingGeometry::NonSharedVertexBuffers ) // was using shared VB but won't use it anymore so update once
//		                          || !DynamicRayTracingGeometry->IsValid ( )
//		                          || DynamicRayTracingGeometry->IsEvicted ( )
//		                          || DynamicRayTracingGeometry->GetRequiresBuild ( );
//
//		bUpdateRayTracingGeometry = bNeedsUpdate;
//	}
//
//	// Setup a new instance
//	FRayTracingInstance RayTracingInstance;
//	RayTracingInstance.Geometry = bUseDynamicGeometry ? DynamicRayTracingGeometry : RayTracingLOD.RayTracingGeometry;
//
//	check ( RayTracingInstance.Geometry->IsInitialized() );
//
//	const FInstanceSceneDataBuffers* InstanceSceneDataBuffers = GetInstanceSceneDataBuffers ( );
//	const int32                      InstanceCount            = InstanceSceneDataBuffers ? InstanceSceneDataBuffers->GetNumInstances ( ) : 1;
//
//	// NOTE: For now, only single-instance dynamic ray tracing is supported
//	if ( InstanceCount > 1 )
//	{
//		static bool bWarnOnce = true;
//		if ( bWarnOnce )
//		{
//			bWarnOnce = false;
//			UE_LOG ( LogStaticMesh , Warning , TEXT("Nanite instanced static mesh using World Position Offset not supported in ray tracing yet (%s).") , *StaticMesh->GetPathName() );
//		}
//
//		return;
//	}
//
//	RayTracingInstance.InstanceTransformsView = MakeArrayView ( &GetLocalToWorld ( ) , 1 );
//	RayTracingInstance.NumTransforms          = 1;
//
//	const int32 NumRayTracingMaterialEntries = RayTracingFallbackLODs [ ValidLODIndex ].Sections.Num ( );
//
//	// Setup the cached materials again when the LOD changes
//	if ( NumRayTracingMaterialEntries != CachedRayTracingMaterials.Num ( ) || ValidLODIndex != CachedRayTracingMaterialsLODIndex )
//	{
//		CachedRayTracingMaterials.Reset ( );
//
//		SetupFallbackRayTracingMaterials ( ValidLODIndex , CachedRayTracingMaterials );
//		CachedRayTracingMaterialsLODIndex = ValidLODIndex;
//	}
//	else
//	{
//		// Skip computing the mask and flags in the renderer since material didn't change
//		RayTracingInstance.bInstanceMaskAndFlagsDirty = false;
//	}
//
//	RayTracingInstance.MaterialsView = CachedRayTracingMaterials;
//
//	if ( bUseDynamicGeometry && bUpdateRayTracingGeometry )
//	{
//		const uint32 NumVertices = RayTracingLOD.VertexBuffers->PositionVertexBuffer.GetNumVertices ( );
//
//		Collector.AddRayTracingGeometryUpdate (
//		                                       FirstActiveViewIndex ,
//		                                       FRayTracingDynamicGeometryUpdateParams
//		                                       {
//			                                       CachedRayTracingMaterials , // TODO: this copy can be avoided if FRayTracingDynamicGeometryUpdateParams supported array views
//			                                       false ,
//			                                       NumVertices ,
//			                                       NumVertices * ( uint32 ) sizeof ( FVector3f ) ,
//			                                       DynamicRayTracingGeometry->Initializer.TotalPrimitiveCount ,
//			                                       DynamicRayTracingGeometry ,
//			                                       DynamicVertexBuffer ,
//			                                       true
//		                                       }
//		                                      );
//	}
//
//	for ( int32 ViewIndex = 0 ; ViewIndex < Views.Num ( ) ; ViewIndex++ )
//	{
//		if ( ( VisibilityMap & ( 1 << ViewIndex ) ) == 0 )
//		{
//			continue;
//		}
//
//		Collector.AddRayTracingInstance ( ViewIndex , RayTracingInstance );
//	}
//}
//
//ERayTracingPrimitiveFlags FLPPNaniteChunkedDynamicMeshProxy::GetCachedRayTracingInstance ( FRayTracingInstance& RayTracingInstance )
//{
//	if ( bDynamicRayTracingGeometry )
//	{
//		// Skip Nanite implementation and use base implementation instead
//		return Super::GetCachedRayTracingInstance ( RayTracingInstance );
//	}
//
//	if ( !( IsVisibleInRayTracing ( ) && ShouldRenderInMainPass ( ) && ( IsDrawnInGame ( ) || AffectsIndirectLightingWhileHidden ( ) || CastsHiddenShadow ( ) ) ) && !IsRayTracingFarField ( ) )
//	{
//		return ERayTracingPrimitiveFlags::Exclude;
//	}
//
//	if ( CVarRayTracingNaniteProxyMeshes.GetValueOnRenderThread ( ) == 0 || !HasRayTracingRepresentation ( ) )
//	{
//		return ERayTracingPrimitiveFlags::Exclude;
//	}
//
//	static const auto RayTracingStaticMeshesCVar = IConsoleManager::Get ( ).FindTConsoleVariableDataInt ( TEXT ( "r.RayTracing.Geometry.StaticMeshes" ) );
//
//	if ( RayTracingStaticMeshesCVar && RayTracingStaticMeshesCVar->GetValueOnRenderThread ( ) <= 0 )
//	{
//		return ERayTracingPrimitiveFlags::Exclude;
//	}
//
//	static const auto RayTracingHISMCVar = IConsoleManager::Get ( ).FindTConsoleVariableDataInt ( TEXT ( "r.RayTracing.Geometry.HierarchicalInstancedStaticMesh" ) );
//
//	if ( bIsHierarchicalInstancedStaticMesh && RayTracingHISMCVar && RayTracingHISMCVar->GetValueOnRenderThread ( ) <= 0 )
//	{
//		return ERayTracingPrimitiveFlags::Exclude;
//	}
//
//	static const auto RayTracingLandscapeGrassCVar = IConsoleManager::Get ( ).FindTConsoleVariableDataInt ( TEXT ( "r.RayTracing.Geometry.LandscapeGrass" ) );
//
//	if ( bIsLandscapeGrass && RayTracingLandscapeGrassCVar && RayTracingLandscapeGrassCVar->GetValueOnRenderThread ( ) <= 0 )
//	{
//		return ERayTracingPrimitiveFlags::Exclude;
//	}
//
//	if ( IsFirstPerson ( ) )
//	{
//		// First person primitives are currently not supported in raytracing as this kind of geometry only makes sense from the camera's point of view.
//		return ERayTracingPrimitiveFlags::Exclude;
//	}
//
//	const bool bUsingNaniteRayTracing = GetRayTracingMode ( ) != ERayTracingMode::Fallback;
//	const bool bIsRayTracingFarField  = IsRayTracingFarField ( );
//
//	// try and find the first valid RT geometry index
//	const int32 ValidLODIndex = GetFirstValidRaytracingGeometryLODIndex ( GetRayTracingMode ( ) );
//	if ( ValidLODIndex == INDEX_NONE )
//	{
//		// Use Skip flag here since Excluded primitives don't get cached ray tracing state updated even if it's marked dirty.
//		// ERayTracingPrimitiveFlags::Exclude should only be used for conditions that will cause proxy to be recreated when they change.
//		ERayTracingPrimitiveFlags ResultFlags = ERayTracingPrimitiveFlags::Skip;
//
//		if ( CoarseMeshStreamingHandle != INDEX_NONE )
//		{
//			// If there is a streaming handle (but no valid LOD available), then give the streaming flag to make sure it's not excluded
//			// It's still needs to be processed during TLAS build because this will drive the streaming of these resources.
//			ResultFlags |= ERayTracingPrimitiveFlags::Streaming;
//		}
//
//		if ( bIsRayTracingFarField )
//		{
//			ResultFlags |= ERayTracingPrimitiveFlags::FarField;
//		}
//
//		return ResultFlags;
//	}
//
//	FStaticMeshRayTracingProxyLODArray& RayTracingLODs = RenderData->RayTracingProxy->LODs;
//
//	if ( bUsingNaniteRayTracing )
//	{
//		RayTracingInstance.Geometry                   = nullptr;
//		RayTracingInstance.bApplyLocalBoundsTransform = false;
//	}
//	else
//	{
//		RayTracingInstance.Geometry                   = RenderData->RayTracingProxy->LODs [ ValidLODIndex ].RayTracingGeometry;
//		RayTracingInstance.bApplyLocalBoundsTransform = false;
//	}
//
//	//checkf(SupportsInstanceDataBuffer() && InstanceSceneData.Num() <= GetPrimitiveSceneInfo()->GetNumInstanceSceneDataEntries(),
//	//	TEXT("Primitives using ERayTracingPrimitiveFlags::CacheInstances require instance transforms available in GPUScene"));
//
//	RayTracingInstance.NumTransforms = GetPrimitiveSceneInfo ( )->GetNumInstanceSceneDataEntries ( );
//	// When ERayTracingPrimitiveFlags::CacheInstances is used, instance transforms are copied from GPUScene while building ray tracing instance buffer.
//
//	if ( bUsingNaniteRayTracing )
//	{
//		SetupRayTracingMaterials ( RayTracingInstance.Materials );
//	}
//	else
//	{
//		SetupFallbackRayTracingMaterials ( ValidLODIndex , RayTracingInstance.Materials );
//	}
//
//	// setup the flags
//	ERayTracingPrimitiveFlags ResultFlags = ERayTracingPrimitiveFlags::CacheInstances;
//
//	if ( CoarseMeshStreamingHandle != INDEX_NONE )
//	{
//		ResultFlags |= ERayTracingPrimitiveFlags::Streaming;
//	}
//
//	if ( bIsRayTracingFarField )
//	{
//		ResultFlags |= ERayTracingPrimitiveFlags::FarField;
//	}
//
//	return ResultFlags;
//}
//
//RayTracing::FGeometryGroupHandle FLPPNaniteChunkedDynamicMeshProxy::GetRayTracingGeometryGroupHandle ( ) const
//{
//	check ( IsInRenderingThread() || IsInParallelRenderingThread() );
//	return RayTracingGeometryGroupHandle;
//}
//
//#endif // RHI_RAYTRACING

//#if RHI_RAYTRACING || NANITE_ENABLE_DEBUG_RENDERING
//
//// Loosely copied from FStaticMeshSceneProxy::SetMeshElementGeometrySource and modified for Nanite fallback
//// TODO: Refactor all this to share common code with Nanite and regular SM scene proxy
//uint32 FLPPNaniteChunkedDynamicMeshProxy::SetMeshElementGeometrySource (
//	const FStaticMeshSection&                Section ,
//	const FFallbackLODInfo::FSectionInfo&    SectionInfo ,
//	const FRawStaticIndexBuffer&             IndexBuffer ,
//	const FAdditionalStaticMeshIndexBuffers* AdditionalIndexBuffers ,
//	const ::FVertexFactory*                  VertexFactory ,
//	bool                                     bWireframe ,
//	bool                                     bUseReversedIndices ,
//	FMeshBatch&                              OutMeshElement ) const
//{
//	if ( Section.NumTriangles == 0 )
//	{
//		return 0;
//	}
//
//	FMeshBatchElement& OutMeshBatchElement = OutMeshElement.Elements [ 0 ];
//	uint32             NumPrimitives       = 0;
//
//	if ( bWireframe )
//	{
//		if ( AdditionalIndexBuffers && AdditionalIndexBuffers->WireframeIndexBuffer.IsInitialized ( ) )
//		{
//			OutMeshElement.Type             = PT_LineList;
//			OutMeshBatchElement.FirstIndex  = 0;
//			OutMeshBatchElement.IndexBuffer = &AdditionalIndexBuffers->WireframeIndexBuffer;
//			NumPrimitives                   = AdditionalIndexBuffers->WireframeIndexBuffer.GetNumIndices ( ) / 2;
//		}
//		else
//		{
//			OutMeshBatchElement.FirstIndex  = 0;
//			OutMeshBatchElement.IndexBuffer = &IndexBuffer;
//			NumPrimitives                   = IndexBuffer.GetNumIndices ( ) / 3;
//
//			OutMeshElement.Type                    = PT_TriangleList;
//			OutMeshElement.bWireframe              = true;
//			OutMeshElement.bDisableBackfaceCulling = true;
//		}
//	}
//	else
//	{
//		OutMeshElement.Type = PT_TriangleList;
//
//		OutMeshBatchElement.IndexBuffer = bUseReversedIndices ? &AdditionalIndexBuffers->ReversedIndexBuffer : &IndexBuffer;
//		OutMeshBatchElement.FirstIndex  = Section.FirstIndex;
//		NumPrimitives                   = Section.NumTriangles;
//	}
//
//	OutMeshBatchElement.NumPrimitives = NumPrimitives;
//	OutMeshElement.VertexFactory      = VertexFactory;
//
//	return NumPrimitives;
//
//
//bool FLPPNaniteChunkedDynamicMeshProxy::IsReversedCullingNeeded ( bool bUseReversedIndices ) const
//{
//	// Use != to ensure consistent face directions between negatively and positively scaled primitives
//	// NOTE: This is only used debug draw mesh elements
//	// (Nanite determines cull mode on the GPU. See ReverseWindingOrder() in NaniteRasterizer.usf)
//	const bool bReverseNeeded = IsCullingReversedByComponent ( ) != IsLocalToWorldDeterminantNegative ( );
//	return bReverseNeeded && !bUseReversedIndices;
//}
//
//#endif

Nanite::FResourceMeshInfo FLPPNaniteChunkedDynamicMeshProxy::GetResourceMeshInfo ( ) const
{
	Nanite::FResourceMeshInfo OutInfo;

	OutInfo.NumClusters  = Resources->NumClusters;
	OutInfo.NumNodes     = Resources->NumHierarchyNodes;
	OutInfo.NumVertices  = Resources->NumInputVertices;
	OutInfo.NumTriangles = Resources->NumInputTriangles;
	OutInfo.NumMaterials = MaterialMaxIndex + 1;
	OutInfo.DebugName    = TEXT ( "LPPNaniteChunkedDynamicMeshProxy" );

	OutInfo.NumResidentClusters = Resources->NumResidentClusters;

	OutInfo.bAssembly = Resources->AssemblyTransforms.Num ( ) > 0;

	// TODO : Map To Material
	//{
	//	OutInfo.NumSegments = RenderData->MaterialMapping.Num ( );
	//
	//	OutInfo.SegmentMapping.Init ( INDEX_NONE , MaterialMaxIndex + 1 );
	//
	//	for ( int32 SectionIndex = 0 ; SectionIndex < RenderData->MaterialMapping.Num ( ) ; ++SectionIndex )
	//	{
	//		OutInfo.SegmentMapping [ RenderData->MaterialMapping [ SectionIndex ] ] = SectionIndex;
	//	}
	//}

	{
		OutInfo.NumSegments = 1;

		OutInfo.SegmentMapping.Init ( 0 , 1 );
	}

	return MoveTemp ( OutInfo );
}

Nanite::FResourcePrimitiveInfo FLPPNaniteChunkedDynamicMeshProxy::GetResourcePrimitiveInfo ( ) const
{
	Nanite::FResourcePrimitiveInfo NewInfo;

	NewInfo.ResourceID              = Resources->RuntimeResourceID;
	NewInfo.HierarchyOffset         = Resources->HierarchyOffset;
	NewInfo.ImposterIndex           = Resources->ImposterIndex;
	NewInfo.AssemblyTransformOffset = Resources->AssemblyTransformOffset;
	NewInfo.AssemblyTransformCount  = Resources->AssemblyTransforms.Num ( );

	return NewInfo;
}

void FLPPNaniteChunkedDynamicMeshProxy::GetDistanceFieldAtlasData ( const class FDistanceFieldVolumeData*& OutDistanceFieldData , float& SelfShadowBias ) const
{
	if ( DistanceFieldPtr.IsValid ( ) )
	{
		OutDistanceFieldData = DistanceFieldPtr.Get ( );
		SelfShadowBias       = DFBias;
	}
}

bool FLPPNaniteChunkedDynamicMeshProxy::HasDistanceFieldRepresentation ( ) const
{
	return CastsDynamicShadow ( ) && AffectsDistanceFieldLighting ( ) && DistanceFieldPtr.IsValid ( );
}

const FCardRepresentationData* FLPPNaniteChunkedDynamicMeshProxy::GetMeshCardRepresentation ( ) const
{
	if ( LumenCardData.IsValid ( ) )
	{
		return LumenCardData.Get ( );
	}

	return nullptr;
}

bool FLPPNaniteChunkedDynamicMeshProxy::IsCollisionView ( const FEngineShowFlags& EngineShowFlags , bool& bDrawSimpleCollision , bool& bDrawComplexCollision ) const
{
	bDrawSimpleCollision = bDrawComplexCollision = false;

	const bool bInCollisionView = EngineShowFlags.CollisionVisibility || EngineShowFlags.CollisionPawn;

#if NANITE_ENABLE_DEBUG_RENDERING
	// If in a 'collision view' and collision is enabled
	if ( bInCollisionView && IsCollisionEnabled ( ) )
	{
		// See if we have a response to the interested channel
		bool bHasResponse = EngineShowFlags.CollisionPawn && CollisionResponse.GetResponse ( ECC_Pawn ) != ECR_Ignore;
		bHasResponse      |= EngineShowFlags.CollisionVisibility && CollisionResponse.GetResponse ( ECC_Visibility ) != ECR_Ignore;

		if ( bHasResponse )
		{
			// Visibility uses complex and pawn uses simple. However, if UseSimpleAsComplex or UseComplexAsSimple is used we need to adjust accordingly
			bDrawComplexCollision = ( EngineShowFlags.CollisionVisibility && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseSimpleAsComplex ) || ( EngineShowFlags.CollisionPawn && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple );
			bDrawSimpleCollision  = ( EngineShowFlags.CollisionPawn && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseComplexAsSimple ) || ( EngineShowFlags.CollisionVisibility && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseSimpleAsComplex );
		}
	}
#endif
	return bInCollisionView;
}

uint32 FLPPNaniteChunkedDynamicMeshProxy::GetMemoryFootprint ( ) const
{
	return sizeof( *this ) + GetAllocatedSize ( );
}
