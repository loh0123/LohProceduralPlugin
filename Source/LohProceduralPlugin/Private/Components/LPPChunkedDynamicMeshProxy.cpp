// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)


#include "Components/LPPChunkedDynamicMeshProxy.h"

#include "MeshPaintVisualize.h"
#include "PrimitiveUniformShaderParametersBuilder.h"
#include "RayTracingInstance.h"
#include "Materials/MaterialRenderProxy.h"

FMeshRenderBufferSet* FLPPChunkedDynamicMeshProxy::AllocateNewRenderBufferSet ( )
{
	// should we hang onto these and destroy them in constructor? leaving to subclass seems risky?
	FMeshRenderBufferSet* RenderBufferSet = new FMeshRenderBufferSet ( GetScene ( ).GetFeatureLevel ( ) );

	RenderBufferSet->Material          = UMaterial::GetDefaultMaterial ( MD_Surface );
	RenderBufferSet->bEnableRaytracing = this->bEnableRaytracing && this->IsVisibleInRayTracing ( );

	AllocatedSetsLock.Lock ( );
	AllocatedBufferSets.Add ( RenderBufferSet );
	AllocatedSetsLock.Unlock ( );

	return RenderBufferSet;
}

void FLPPChunkedDynamicMeshProxy::ReleaseRenderBufferSet ( FMeshRenderBufferSet* BufferSet )
{
	FScopeLock Lock ( &AllocatedSetsLock );
	if ( ensure ( AllocatedBufferSets.Contains(BufferSet) ) )
	{
		AllocatedBufferSets.Remove ( BufferSet );
		Lock.Unlock ( );

		FMeshRenderBufferSet::DestroyRenderBufferSet ( BufferSet );
	}
}

void FLPPChunkedDynamicMeshProxy::Initialize ( const TFunctionRef < bool  ( TArray < FVector3f >& PositionList , TArray < uint32 >& IndexList , TArray < FVector2f >& UV0List , TArray < FColor >& ColorList , TArray < FVector3f >& NormalList , TArray < FVector3f >& TangentList , TArray < FVector3f >& BiTangentList ) >& InitFunc )
{
	// allocate buffer sets based on materials
	ensure ( AllocatedBufferSets.IsEmpty ( ) );
	FMeshRenderBufferSet* RenderBufferSet = AllocateNewRenderBufferSet ( );
	RenderBufferSet->Material             = ParentComponent->GetMaterial ( 0 );

	if ( RenderBufferSet->Material == nullptr )
	{
		RenderBufferSet->Material = UMaterial::GetDefaultMaterial ( MD_Surface );
	}

	TArray < FVector3f > PositionList;
	TArray < uint32 >    IndexList;
	TArray < FVector2f > UV0List;
	TArray < FColor >    ColorList;
	TArray < FVector3f > NormalList;
	TArray < FVector3f > TangentList;
	TArray < FVector3f > BiTangentList;

	if ( InitFunc ( PositionList , IndexList , UV0List , ColorList , NormalList , TangentList , BiTangentList ) == false )
	{
		return;
	}

	RenderBufferSet->TriangleCount = IndexList.Num ( ) / 3;
	RenderBufferSet->PositionVertexBuffer.Init ( PositionList , false );
	RenderBufferSet->IndexBuffer.Indices = IndexList;

	RenderBufferSet->StaticMeshVertexBuffer.Init ( PositionList.Num ( ) , 1 , false );

	if ( ColorList.IsEmpty ( ) == false )
	{
		RenderBufferSet->ColorVertexBuffer.InitFromColorArray ( ColorList.GetData ( ) , ColorList.Num ( ) , sizeof ( FColor ) , false );
	}

	for ( int32 VertexIndex = 0 ; VertexIndex < PositionList.Num ( ) ; ++VertexIndex )
	{
		RenderBufferSet->StaticMeshVertexBuffer.SetVertexUV ( VertexIndex , 0 , UV0List [ VertexIndex ] );
		RenderBufferSet->StaticMeshVertexBuffer.SetVertexTangents (
		                                                           VertexIndex ,
		                                                           TangentList [ VertexIndex ] ,
		                                                           BiTangentList [ VertexIndex ] ,
		                                                           NormalList [ VertexIndex ]
		                                                          );
	}

	ENQUEUE_RENDER_COMMAND ( FChunbkedDynamicMeshSceneProxyInitializeSingle ) (
	                                                                           [RenderBufferSet] ( FRHICommandListImmediate& RHICmdList )
	                                                                           {
		                                                                           RenderBufferSet->Upload ( );
	                                                                           } );
}

void FLPPChunkedDynamicMeshProxy::InitializeFromMesh ( const FDynamicMesh3* MeshData )
{
	if ( MeshData == nullptr )
	{
		return;
	}

	if ( MeshData->TriangleCount ( ) != 0 )
	{
		/** Vertex position */
		TArray < FVector3f > MeshPosition = TArray < FVector3f > ( );

		/** Vertex normal */
		TArray < FVector3f > MeshNormal = TArray < FVector3f > ( );

		/** Vertex color */
		TArray < FColor > MeshColor = TArray < FColor > ( );

		/** Vertex texture co-ordinate */
		TArray < FVector2f > MeshUV0 = TArray < FVector2f > ( );

		const int NumTriangles  = MeshData->TriangleCount ( );
		const int NumVertices   = NumTriangles * 3;
		const int NumUVOverlays = MeshData->HasAttributes ( ) ? MeshData->Attributes ( )->NumUVLayers ( ) : 0;

		const FDynamicMeshNormalOverlay* NormalOverlay = MeshData->HasAttributes ( ) ? MeshData->Attributes ( )->PrimaryNormals ( ) : nullptr;
		const FDynamicMeshColorOverlay*  ColorOverlay  = MeshData->HasAttributes ( ) ? MeshData->Attributes ( )->PrimaryColors ( ) : nullptr;

		const bool bHasColor    = ColorOverlay != nullptr;
		const bool bHasTangents = MeshData->HasAttributes ( ) && MeshData->Attributes ( )->HasTangentSpace ( );

		UE::Geometry::FDynamicMeshTangents Tangents ( MeshData );

		{
			MeshPosition.AddUninitialized ( NumVertices );
			MeshNormal.AddUninitialized ( NumVertices );
			MeshUV0.AddUninitialized ( NumVertices );

			if ( bHasColor )
			{
				MeshColor.AddUninitialized ( NumVertices );
			}
		}

		// populate the triangle array.  we use this for parallelism. 
		TArray < int32 > TriangleArray;
		{
			TriangleArray.Reserve ( NumTriangles );
			for ( const int32 TriangleID : MeshData->TriangleIndicesItr ( ) )
			{
				if ( MeshData->IsTriangle ( TriangleID ) == false )
				{
					continue;
				}

				TriangleArray.Add ( TriangleID );
			}
		}

		ParallelFor ( TriangleArray.Num ( ) , [&] ( int idx )
		{
			const int32 TriangleID = TriangleArray [ idx ];

			UE::Geometry::FIndex3i TriIndexList       = MeshData->GetTriangle ( TriangleID );
			UE::Geometry::FIndex3i TriNormalIndexList = ( NormalOverlay != nullptr ) ? NormalOverlay->GetTriangle ( TriangleID ) : UE::Geometry::FIndex3i::Invalid ( );
			UE::Geometry::FIndex3i TriColorIndexList  = ( ColorOverlay != nullptr ) ? ColorOverlay->GetTriangle ( TriangleID ) : UE::Geometry::FIndex3i::Invalid ( );

			const int32 VertOffset = idx * 3;
			for ( int32 IndexOffset = 0 ; IndexOffset < 3 ; ++IndexOffset )
			{
				const int32 ListIndex     = VertOffset + IndexOffset;
				const int32 TriangleIndex = TriIndexList [ IndexOffset ];
				const int32 NormalIndex   = TriNormalIndexList [ IndexOffset ];
				const int32 ColorIndex    = TriColorIndexList [ IndexOffset ];

				MeshPosition [ ListIndex ] = static_cast < FVector3f > ( MeshData->GetVertex ( TriangleIndex ) );
				MeshNormal [ ListIndex ]   = NormalIndex != FDynamicMesh3::InvalidID ? NormalOverlay->GetElement ( NormalIndex ) : MeshData->GetVertexNormal ( TriangleIndex );

				if ( bHasColor )
				{
					MeshColor [ ListIndex ] = static_cast < FLinearColor > ( ColorIndex != FDynamicMesh3::InvalidID ? ColorOverlay->GetElement ( ColorIndex ) : static_cast < FVector4f > ( MeshData->GetVertexColor ( TriangleIndex ) ) ).ToFColor ( true );
				}
			}

			{
				//MeshNormal [ idx ] = NormalIndex != FDynamicMesh3::InvalidID ? NormalOverlay->GetElement ( NormalIndex ) : MeshData->GetVertexNormal ( TriangleIndex );
				//
				//if ( bHasTangents )
				//{
				//	Tangents.GetTangentVectors ( TriangleID , IndexOffset , MeshNormal [ ListIndex ] , MeshTangent [ ListIndex ] , MeshBiTangent [ ListIndex ] );
				//}
				//else
				//{
				//	UE::Geometry::VectorUtil::MakePerpVectors ( MeshNormal [ ListIndex ] , MeshTangent [ ListIndex ] , MeshBiTangent [ ListIndex ] );
				//}
			}

			for ( int32 TexIndex = 0 ; TexIndex < 1 /*NumTexCoords */; ++TexIndex )
			{
				const FDynamicMeshUVOverlay* UVOverlay = MeshData->HasAttributes ( ) ? MeshData->Attributes ( )->GetUVLayer ( TexIndex ) : nullptr;

				UE::Geometry::FIndex3i TriUVIndexList = ( UVOverlay != nullptr ) ? UVOverlay->GetTriangle ( TriangleID ) : UE::Geometry::FIndex3i::Invalid ( );

				for ( int32 IndexOffset = 0 ; IndexOffset < 3 ; ++IndexOffset )
				{
					const int32 ListIndex = VertOffset + IndexOffset;
					const int32 UVIndex   = TriUVIndexList [ IndexOffset ];

					MeshUV0 [ ListIndex ] = ( UVIndex != FDynamicMesh3::InvalidID ) ? UVOverlay->GetElement ( UVIndex ) : FVector2f::Zero ( );
				}
			}
		} );

		Initialize ( [&] (
		            TArray < FVector3f >& PositionList ,
		            TArray < uint32 >&    IndexList ,
		            TArray < FVector2f >& UV0List ,
		            TArray < FColor >&    ColorList ,
		            TArray < FVector3f >& NormalList ,
		            TArray < FVector3f >& TangentList ,
		            TArray < FVector3f >& BiTangentList
		            )
		            {
			            if ( MeshPosition.IsEmpty ( ) )
			            {
				            return false;
			            }

			            PositionList = MeshPosition;
			            UV0List      = MeshUV0;
			            ColorList    = MeshColor;
			            NormalList   = MeshNormal;
			            //TangentList   = MeshTangent;
			            //BiTangentList = MeshBiTangent;

			            //NormalList.AddUninitialized ( MeshPosition.Num ( ) );
			            TangentList.AddUninitialized ( MeshPosition.Num ( ) );
			            BiTangentList.AddUninitialized ( MeshPosition.Num ( ) );

			            IndexList.Reserve ( MeshPosition.Num ( ) );

			            for ( int32 Index = 0 ; Index < MeshPosition.Num ( ) ; ++Index )
			            {
				            IndexList.Add ( Index );

				            UE::Geometry::VectorUtil::MakePerpVectors ( MeshNormal [ Index ] , TangentList [ Index ] , BiTangentList [ Index ] );
			            }

			            return true;
		            }
		           );
	}
}

// Distance Field And Lumen

void FLPPChunkedDynamicMeshProxy::SetDistanceFieldData ( const TSharedPtr < FDistanceFieldVolumeData >& InDistanceFieldData )
{
	DistanceFieldData                    = InDistanceFieldData;
	bSupportsDistanceFieldRepresentation = true;

	bAffectDistanceFieldLighting = ParentComponent->bAffectDistanceFieldLighting;

	UpdateVisibleInLumenScene ( );
}

void FLPPChunkedDynamicMeshProxy::SetLumenData ( const TArray < class FLumenCardBuildData >& InLumenCardData , const FBox& InLumenBound )
{
	if ( MeshCards.IsValid ( ) == false )
	{
		MeshCards = MakePimpl < FCardRepresentationData > ( );
	}

	FMeshCardsBuildData& MeshCardData = MeshCards.Get ( )->MeshCardsBuildData;

	MeshCardData.bMostlyTwoSided = false;
	MeshCardData.Bounds          = InLumenBound;
	MeshCardData.CardBuildData   = InLumenCardData;

	UpdateVisibleInLumenScene ( );
}

void FLPPChunkedDynamicMeshProxy::GetDistanceFieldAtlasData ( const class FDistanceFieldVolumeData*& OutDistanceFieldData , float& SelfShadowBias ) const
{
	if ( DistanceFieldData.IsValid ( ) )
	{
		OutDistanceFieldData = DistanceFieldData.Get ( );
		SelfShadowBias       = DFBias;
	}
}

bool FLPPChunkedDynamicMeshProxy::HasDistanceFieldRepresentation ( ) const
{
	return CastsDynamicShadow ( ) && AffectsDistanceFieldLighting ( ) && DistanceFieldData.IsValid ( );
}

bool FLPPChunkedDynamicMeshProxy::HasDynamicIndirectShadowCasterRepresentation ( ) const
{
	return bCastsDynamicIndirectShadow && DistanceFieldData.IsValid ( );
}

const FCardRepresentationData* FLPPChunkedDynamicMeshProxy::GetMeshCardRepresentation ( ) const
{
	return MeshCards.Get ( );
}

// Dynamic Render

FMaterialRenderProxy* FLPPChunkedDynamicMeshProxy::GetEngineVertexColorMaterialProxy ( FMeshElementCollector& Collector , const FEngineShowFlags& EngineShowFlags , bool bProxyIsSelected , bool bIsHovered )
{
	FMaterialRenderProxy* ForceOverrideMaterialProxy = nullptr;
#if UE_ENABLE_DEBUG_DRAWING
	if ( bProxyIsSelected && EngineShowFlags.VertexColors && AllowDebugViewmodes ( ) )
	{
		// Note: static mesh renderer does something more complicated involving per-section selection, but whole component selection seems ok for now.
		if ( FMaterialRenderProxy* VertexColorVisualizationMaterialInstance = MeshPaintVisualize::GetMaterialRenderProxy ( bProxyIsSelected , bIsHovered ) )
		{
			Collector.RegisterOneFrameMaterialProxy ( VertexColorVisualizationMaterialInstance );
			ForceOverrideMaterialProxy = VertexColorVisualizationMaterialInstance;
		}
	}
#endif
	return ForceOverrideMaterialProxy;
}

void FLPPChunkedDynamicMeshProxy::GetDynamicMeshElements ( const TArray < const FSceneView* >& Views , const FSceneViewFamily& ViewFamily , uint32 VisibilityMap , FMeshElementCollector& Collector ) const
{
	QUICK_SCOPE_CYCLE_COUNTER ( STAT_BaseDynamicMeshSceneProxy_GetDynamicMeshElements );

	const FEngineShowFlags& EngineShowFlags      = ViewFamily.EngineShowFlags;
	bool                    bIsWireframeViewMode = ( AllowDebugViewmodes ( ) && EngineShowFlags.Wireframe );
	bool                    bWireframe           = bIsWireframeViewMode;
	const bool              bProxyIsSelected     = IsSelected ( );


	TArray < FMeshRenderBufferSet* > Buffers = AllocatedBufferSets;

#if UE_ENABLE_DEBUG_DRAWING
	bool       bDrawSimpleCollision = false , bDrawComplexCollision = false;
	const bool bDrawCollisionView   = IsCollisionView ( EngineShowFlags , bDrawSimpleCollision , bDrawComplexCollision );

	// If we're in a collision view, run the only draw the collision and return without drawing mesh normally
	if ( bDrawCollisionView )
	{
		GetCollisionDynamicMeshElements ( Buffers , EngineShowFlags , bDrawCollisionView , bDrawSimpleCollision , bDrawComplexCollision , bProxyIsSelected , Views , VisibilityMap , Collector );
		return;
	}
#endif

	// Get wireframe material proxy if requested and available, otherwise disable wireframe
	FMaterialRenderProxy* WireframeMaterialProxy = nullptr;
	if ( bWireframe )
	{
		UMaterialInterface* WireframeMaterial = GEngine->WireframeMaterial;
		if ( WireframeMaterial != nullptr )
		{
			FLinearColor                 UseWireframeColor         = ( bProxyIsSelected && bIsWireframeViewMode ) ? GEngine->GetSelectedMaterialColor ( ) : FLinearColor ( 1.0f , 1.0f , 1.0f );
			FColoredMaterialRenderProxy* WireframeMaterialInstance = new FColoredMaterialRenderProxy (
			                                                                                          WireframeMaterial->GetRenderProxy ( ) , UseWireframeColor );
			Collector.RegisterOneFrameMaterialProxy ( WireframeMaterialInstance );
			WireframeMaterialProxy = WireframeMaterialInstance;
		}
		else
		{
			bWireframe = false;
		}
	}

	FMaterialRenderProxy* ForceOverrideMaterialProxy = GetEngineVertexColorMaterialProxy ( Collector , EngineShowFlags , bProxyIsSelected , IsHovered ( ) );

	ESceneDepthPriorityGroup DepthPriority = SDPG_World;


	for ( int32 ViewIndex = 0 ; ViewIndex < Views.Num ( ) ; ViewIndex++ )
	{
		if ( VisibilityMap & ( 1 << ViewIndex ) )
		{
			// Draw the mesh.
			for ( FMeshRenderBufferSet* BufferSet : Buffers )
			{
				FMaterialRenderProxy* MaterialProxy = ForceOverrideMaterialProxy;
				if ( !MaterialProxy )
				{
					UMaterialInterface* UseMaterial = BufferSet->Material;
					MaterialProxy                   = UseMaterial->GetRenderProxy ( );
				}

				if ( BufferSet->TriangleCount == 0 )
				{
					continue;
				}

				// lock buffers so that they aren't modified while we are submitting them
				FScopeLock BuffersLock ( &BufferSet->BuffersLock );

				// do we need separate one of these for each MeshRenderBufferSet?
				FDynamicPrimitiveUniformBuffer&          DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource < FDynamicPrimitiveUniformBuffer > ( );
				FPrimitiveUniformShaderParametersBuilder Builder;
				BuildUniformShaderParameters ( Builder );
				DynamicPrimitiveUniformBuffer.Set ( Collector.GetRHICommandList ( ) , Builder );

				// If we want Wireframe-on-Shaded, we have to draw the solid. If View Mode Overrides are enabled, the solid
				// will be replaced with it's wireframe, so we might as well not. 
				bool bDrawSolidWithWireframe = ( bIsWireframeViewMode == false );

				if ( BufferSet->IndexBuffer.Indices.Num ( ) > 0 )
				{
					if ( bWireframe )
					{
						if ( bDrawSolidWithWireframe )
						{
							DrawBatch ( Collector , *BufferSet , BufferSet->IndexBuffer , MaterialProxy , /*bWireframe*/false , DepthPriority , ViewIndex , DynamicPrimitiveUniformBuffer );
						}
						DrawBatch ( Collector , *BufferSet , BufferSet->IndexBuffer , WireframeMaterialProxy , /*bWireframe*/true , DepthPriority , ViewIndex , DynamicPrimitiveUniformBuffer );
					}
					else
					{
						DrawBatch ( Collector , *BufferSet , BufferSet->IndexBuffer , MaterialProxy , /*bWireframe*/false , DepthPriority , ViewIndex , DynamicPrimitiveUniformBuffer );
					}
				}
			}
		}
	}

#if UE_ENABLE_DEBUG_DRAWING
	GetCollisionDynamicMeshElements ( Buffers , EngineShowFlags , bDrawCollisionView , bDrawSimpleCollision , bDrawComplexCollision , bProxyIsSelected , Views , VisibilityMap , Collector );
#endif
}

void FLPPChunkedDynamicMeshProxy::GetCollisionDynamicMeshElements ( TArray < FMeshRenderBufferSet* >&   Buffers ,
                                                                    const FEngineShowFlags&             EngineShowFlags , bool bDrawCollisionView , bool              bDrawSimpleCollision , bool bDrawComplexCollision , bool bProxyIsSelected ,
                                                                    const TArray < const FSceneView* >& Views , uint32         VisibilityMap , FMeshElementCollector& Collector ) const
{
#if UE_ENABLE_DEBUG_DRAWING
	FScopeLock Lock ( &CachedCollisionLock );

	if ( !bHasCollisionData )
	{
		return;
	}

	// Note: This is closely following StaticMeshSceneProxy.cpp's collision rendering code, from its GetDynamicMeshElements() implementation
	FColor SimpleCollisionColor  = FColor ( 157 , 149 , 223 , 255 );
	FColor ComplexCollisionColor = FColor ( 0 , 255 , 255 , 255 );

	for ( int32 ViewIndex = 0 ; ViewIndex < Views.Num ( ) ; ViewIndex++ )
	{
		if ( VisibilityMap & ( 1 << ViewIndex ) )
		{
			if ( AllowDebugViewmodes ( ) )
			{
				// Should we draw the mesh wireframe to indicate we are using the mesh as collision
				bool bDrawComplexWireframeCollision = ( EngineShowFlags.Collision && IsCollisionEnabled ( ) && CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple );

				// If drawing complex collision as solid or wireframe
				if ( bHasComplexMeshData && ( bDrawComplexWireframeCollision || ( bDrawCollisionView && bDrawComplexCollision ) ) )
				{
					bool bDrawWireframe = !bDrawCollisionView;

					UMaterial*   MaterialToUse      = GEngine->ShadedLevelColorationUnlitMaterial;
					FLinearColor DrawCollisionColor = GetWireframeColor ( );
					// Collision view modes draw collision mesh as solid
					if ( bDrawCollisionView == false )
					{
						MaterialToUse      = GEngine->WireframeMaterial;
						DrawCollisionColor = ( CollisionTraceFlag == ECollisionTraceFlag::CTF_UseComplexAsSimple ) ? SimpleCollisionColor : ComplexCollisionColor;
					}
					// Create colored proxy
					FColoredMaterialRenderProxy* CollisionMaterialInstance = new FColoredMaterialRenderProxy ( MaterialToUse->GetRenderProxy ( ) , DrawCollisionColor );
					Collector.RegisterOneFrameMaterialProxy ( CollisionMaterialInstance );

					// Draw the mesh with collision materials
					for ( FMeshRenderBufferSet* BufferSet : Buffers )
					{
						if ( BufferSet->TriangleCount == 0 )
						{
							continue;
						}

						// lock buffers so that they aren't modified while we are submitting them
						FScopeLock BuffersLock ( &BufferSet->BuffersLock );

						// do we need separate one of these for each MeshRenderBufferSet?
						FDynamicPrimitiveUniformBuffer&          DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource < FDynamicPrimitiveUniformBuffer > ( );
						FPrimitiveUniformShaderParametersBuilder Builder;
						BuildUniformShaderParameters ( Builder );
						DynamicPrimitiveUniformBuffer.Set ( Collector.GetRHICommandList ( ) , Builder );

						if ( BufferSet->IndexBuffer.Indices.Num ( ) > 0 )
						{
							DrawBatch ( Collector , *BufferSet , BufferSet->IndexBuffer , CollisionMaterialInstance , bDrawWireframe , SDPG_World , ViewIndex , DynamicPrimitiveUniformBuffer );
						}
					}
				}
			}

			// Draw simple collision as wireframe if 'show collision', collision is enabled, and we are not using the complex as the simple
			const bool bDrawSimpleWireframeCollision = ( EngineShowFlags.Collision && IsCollisionEnabled ( ) && CollisionTraceFlag != ECollisionTraceFlag::CTF_UseComplexAsSimple );

			if ( ( bDrawSimpleCollision || bDrawSimpleWireframeCollision ) )
			{
				if ( ParentComponent->GetBodySetup ( ) )
				{
					// Avoid zero scaling, otherwise GeomTransform below will assert
					if ( FMath::Abs ( GetLocalToWorld ( ).Determinant ( ) ) > UE_SMALL_NUMBER )
					{
						const bool bDrawSolid = !bDrawSimpleWireframeCollision;

						if ( AllowDebugViewmodes ( ) && bDrawSolid )
						{
							// Make a material for drawing solid collision stuff
							FColoredMaterialRenderProxy* SolidMaterialInstance = new FColoredMaterialRenderProxy (
							                                                                                      GEngine->ShadedLevelColorationUnlitMaterial->GetRenderProxy ( ) ,
							                                                                                      GetWireframeColor ( )
							                                                                                     );

							Collector.RegisterOneFrameMaterialProxy ( SolidMaterialInstance );

							FTransform GeomTransform ( GetLocalToWorld ( ) );
							CachedAggGeom.GetAggGeom ( GeomTransform , GetWireframeColor ( ).ToFColor ( true ) , SolidMaterialInstance , false , true , AlwaysHasVelocity ( ) , ViewIndex , Collector );
						}
						// wireframe
						else
						{
							FTransform GeomTransform ( GetLocalToWorld ( ) );
							CachedAggGeom.GetAggGeom ( GeomTransform , GetSelectionColor ( SimpleCollisionColor , bProxyIsSelected , IsHovered ( ) ).ToFColor ( true ) , NULL , bOwnerIsNull , false , AlwaysHasVelocity ( ) , ViewIndex , Collector );
						}

						// Note: if dynamic mesh component could have nav collision data, we'd also draw that here (see the similar code in StaticMeshRenderer.cpp)
					}
				}
			}
		}
	}
#endif // UE_ENABLE_DEBUG_DRAWING
}

// Static Render

void FLPPChunkedDynamicMeshProxy::DrawBatch ( FMeshElementCollector& Collector , const FMeshRenderBufferSet& RenderBuffers , const FDynamicMeshIndexBuffer32& IndexBuffer , FMaterialRenderProxy* UseMaterial , bool bWireframe , ESceneDepthPriorityGroup DepthPriority , int ViewIndex , FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer ) const
{
	FMeshBatch&        Mesh         = Collector.AllocateMesh ( );
	FMeshBatchElement& BatchElement = Mesh.Elements [ 0 ];
	BatchElement.IndexBuffer        = &IndexBuffer;
	Mesh.bWireframe                 = bWireframe;
	Mesh.bDisableBackfaceCulling    = bWireframe;
	Mesh.VertexFactory              = &RenderBuffers.VertexFactory;
	Mesh.MaterialRenderProxy        = UseMaterial;

	BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;

	BatchElement.FirstIndex     = 0;
	BatchElement.NumPrimitives  = IndexBuffer.Indices.Num ( ) / 3;
	BatchElement.MinVertexIndex = 0;
	BatchElement.MaxVertexIndex = RenderBuffers.PositionVertexBuffer.GetNumVertices ( ) - 1;
	Mesh.ReverseCulling         = IsLocalToWorldDeterminantNegative ( );
	Mesh.Type                   = PT_TriangleList;
	Mesh.DepthPriorityGroup     = DepthPriority;
	// if this is a wireframe draw pass then we do not want to apply View Mode Overrides
	Mesh.bCanApplyViewModeOverrides = bWireframe == false;
	Collector.AddMesh ( ViewIndex , Mesh );
}

bool FLPPChunkedDynamicMeshProxy::AllowStaticDrawPath ( const FSceneView* View ) const
{
	bool bAllowDebugViews = AllowDebugViewmodes ( );
	if ( !bAllowDebugViews )
	{
		return true;
	}
	const FEngineShowFlags& EngineShowFlags = View->Family->EngineShowFlags;
	bool                    bWireframe      = EngineShowFlags.Wireframe;
	if ( bWireframe )
	{
		return false;
	}
	bool bDrawSimpleCollision = false , bDrawComplexCollision = false;
	bool bDrawCollisionView   = IsCollisionView ( EngineShowFlags , bDrawSimpleCollision , bDrawComplexCollision ); // check for the full collision views
	bool bDrawCollisionFlags  = EngineShowFlags.Collision && IsCollisionEnabled ( );                                // check for single component collision rendering
	bool bDrawCollision       = bDrawCollisionFlags || bDrawSimpleCollision || bDrawCollisionView;
	if ( bDrawCollision )
	{
		return false;
	}
	bool bIsSelected     = IsSelected ( );
	bool bColorOverrides = bIsSelected && EngineShowFlags.VertexColors;
	return !bColorOverrides;
}

void FLPPChunkedDynamicMeshProxy::DrawStaticElements ( FStaticPrimitiveDrawInterface* PDI )
{
	QUICK_SCOPE_CYCLE_COUNTER ( STAT_BaseDynamicMeshSceneProxy_DrawStaticElements );

	if ( !bPreferStaticDrawPath )
	{
		return;
	}

	ESceneDepthPriorityGroup DepthPriority = SDPG_World;

	TArray < FMeshRenderBufferSet* > Buffers = AllocatedBufferSets;
	PDI->ReserveMemoryForMeshes ( Buffers.Num ( ) );

	// Draw the mesh.
	int32 SectionIndexCounter = 0;
	for ( FMeshRenderBufferSet* BufferSet : Buffers )
	{
		if ( BufferSet->TriangleCount == 0 )
		{
			continue;
		}

		UMaterialInterface*   UseMaterial   = BufferSet->Material;
		FMaterialRenderProxy* MaterialProxy = UseMaterial->GetRenderProxy ( );

		// lock buffers so that they aren't modified while we are submitting them
		FScopeLock BuffersLock ( &BufferSet->BuffersLock );

		FMeshBatch MeshBatch;

		FMeshBatchElement& BatchElement = MeshBatch.Elements [ 0 ];
		BatchElement.IndexBuffer        = &BufferSet->IndexBuffer;
		MeshBatch.VertexFactory         = &BufferSet->VertexFactory;
		MeshBatch.MaterialRenderProxy   = MaterialProxy;

		BatchElement.PrimitiveUniformBuffer = GetUniformBuffer ( );
		BatchElement.NumPrimitives          = BufferSet->IndexBuffer.Indices.Num ( ) / 3;
		BatchElement.FirstIndex             = 0;
		BatchElement.MinVertexIndex         = 0;
		BatchElement.MaxVertexIndex         = BufferSet->PositionVertexBuffer.GetNumVertices ( ) - 1;
		MeshBatch.ReverseCulling            = IsLocalToWorldDeterminantNegative ( );
		MeshBatch.Type                      = PT_TriangleList;
		MeshBatch.DepthPriorityGroup        = DepthPriority;
		MeshBatch.LODIndex                  = 0;
		MeshBatch.SegmentIndex              = SectionIndexCounter;
		MeshBatch.MeshIdInPrimitive         = SectionIndexCounter;
		SectionIndexCounter++;

		MeshBatch.LCI                    = nullptr; // lightmap cache interface (allowed to be null)
		MeshBatch.CastShadow             = true;
		MeshBatch.bUseForMaterial        = true;
		MeshBatch.bDitheredLODTransition = false;
		MeshBatch.bUseForDepthPass       = true;
		MeshBatch.bUseAsOccluder         = ShouldUseAsOccluder ( );

		PDI->DrawMesh ( MeshBatch , FLT_MAX );
	}
}

// Material

void FLPPChunkedDynamicMeshProxy::UpdatedReferencedMaterials ( )
{
#if WITH_EDITOR
	TArray < UMaterialInterface* > Materials;
	ParentComponent->GetUsedMaterials ( Materials , true );

	// Temporarily disable material verification while the enqueued render command is in flight.
	// The original value for bVerifyUsedMaterials gets restored when the command is executed.
	// If we do not do this, material verification might spuriously fail in cases where the render command for changing
	// the verfifcation material is still in flight but the render thread is already trying to render the mesh.
	const uint8 bRestoreVerifyUsedMaterials = bVerifyUsedMaterials;
	bVerifyUsedMaterials                    = false;

	ENQUEUE_RENDER_COMMAND ( FMeshRenderBufferSetDestroy ) (
	                                                        [this, Materials, bRestoreVerifyUsedMaterials] ( FRHICommandListImmediate& RHICmdList )
	                                                        {
		                                                        this->SetUsedMaterialForVerification ( Materials );
		                                                        this->bVerifyUsedMaterials = bRestoreVerifyUsedMaterials;
	                                                        } );
#endif

	// The material relevance may need updating.
	MaterialRelevance = ParentComponent->GetMaterialRelevance ( GetScene ( ).GetShaderPlatform ( ) );
}

// Collision View

void FLPPChunkedDynamicMeshProxy::SetCollisionData ( )
{
#if UE_ENABLE_DEBUG_DRAWING
	FScopeLock Lock ( &CachedCollisionLock );
	bHasCollisionData   = true;
	bOwnerIsNull        = ParentComponent->GetOwner ( ) == nullptr;
	bHasComplexMeshData = false;
	if ( UBodySetup* BodySetup = ParentComponent->GetBodySetup ( ) )
	{
		CollisionTraceFlag = BodySetup->GetCollisionTraceFlag ( );
		CachedAggGeom      = BodySetup->AggGeom;

		if ( IInterface_CollisionDataProvider* CDP = Cast < IInterface_CollisionDataProvider > ( ParentComponent ) )
		{
			bHasComplexMeshData = CDP->ContainsPhysicsTriMeshData ( BodySetup->bMeshCollideAll );
		}
	}
	else
	{
		CachedAggGeom = FKAggregateGeom ( );
	}
	CollisionResponse = ParentComponent->GetCollisionResponseToChannels ( );
#endif
}

bool FLPPChunkedDynamicMeshProxy::IsCollisionView ( const FEngineShowFlags& EngineShowFlags , bool& bDrawSimpleCollision , bool& bDrawComplexCollision ) const
{
	bDrawSimpleCollision = bDrawComplexCollision = false;

	bool bDrawCollisionView = ( EngineShowFlags.CollisionVisibility || EngineShowFlags.CollisionPawn );

#if UE_ENABLE_DEBUG_DRAWING
	// If in a 'collision view' and collision is enabled
	FScopeLock Lock ( &CachedCollisionLock );
	if ( bHasCollisionData && bDrawCollisionView && IsCollisionEnabled ( ) )
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
	return bDrawCollisionView;
}

#if RHI_RAYTRACING

bool FLPPChunkedDynamicMeshProxy::IsRayTracingRelevant ( ) const
{
	return true;
}

bool FLPPChunkedDynamicMeshProxy::HasRayTracingRepresentation ( ) const
{
	return true;
}

void FLPPChunkedDynamicMeshProxy::GetDynamicRayTracingInstances ( FRayTracingInstanceCollector& Collector )
{
	QUICK_SCOPE_CYCLE_COUNTER ( STAT_BaseDynamicMeshSceneProxy_GetDynamicRayTracingInstances );

	ESceneDepthPriorityGroup DepthPriority = SDPG_World;

	TArray < FMeshRenderBufferSet* > Buffers = AllocatedBufferSets;

	// is it safe to share this between primary and secondary raytracing batches?
	FDynamicPrimitiveUniformBuffer&          DynamicPrimitiveUniformBuffer = Collector.AllocateOneFrameResource < FDynamicPrimitiveUniformBuffer > ( );
	FPrimitiveUniformShaderParametersBuilder Builder;
	BuildUniformShaderParameters ( Builder );
	DynamicPrimitiveUniformBuffer.Set ( Collector.GetRHICommandList ( ) , Builder );

	// Draw the active buffer sets
	for ( FMeshRenderBufferSet* BufferSet : Buffers )
	{
		UMaterialInterface*   UseMaterial   = BufferSet->Material;
		FMaterialRenderProxy* MaterialProxy = UseMaterial->GetRenderProxy ( );

		if ( BufferSet->TriangleCount == 0 )
		{
			continue;
		}
		if ( BufferSet->bIsRayTracingDataValid == false )
		{
			continue;
		}

		// Lock buffers so that they aren't modified while we are submitting them.
		FScopeLock BuffersLock ( &BufferSet->BuffersLock );

		// draw primary index buffer
		if ( BufferSet->IndexBuffer.Indices.Num ( ) > 0
		     && BufferSet->PrimaryRayTracingGeometry.IsValid ( ) )
		{
			ensure ( BufferSet->PrimaryRayTracingGeometry.Initializer.IndexBuffer.IsValid() );
			DrawRayTracingBatch ( Collector , *BufferSet , BufferSet->IndexBuffer , BufferSet->PrimaryRayTracingGeometry , MaterialProxy , DepthPriority , DynamicPrimitiveUniformBuffer );
		}
	}
}

void FLPPChunkedDynamicMeshProxy::DrawRayTracingBatch ( FRayTracingInstanceCollector& Collector , const FMeshRenderBufferSet& RenderBuffers , const FDynamicMeshIndexBuffer32& IndexBuffer , FRayTracingGeometry& RayTracingGeometry , FMaterialRenderProxy* UseMaterialProxy , ESceneDepthPriorityGroup DepthPriority , FDynamicPrimitiveUniformBuffer& DynamicPrimitiveUniformBuffer ) const
{
	ensure ( RayTracingGeometry.Initializer.IndexBuffer.IsValid() );

	TConstArrayView < const FSceneView* > Views         = Collector.GetViews ( );
	const uint32                          VisibilityMap = Collector.GetVisibilityMap ( );

	const int32 FirstActiveViewIndex = FMath::CountTrailingZeros ( VisibilityMap );
	checkf ( Views.IsValidIndex(FirstActiveViewIndex) , TEXT("There should be at least one active view when calling GetDynamicRayTracingInstances(...).") );

	const FSceneView* FirstActiveView = Views [ FirstActiveViewIndex ];

	FRayTracingInstance RayTracingInstance;
	RayTracingInstance.Geometry = &RayTracingGeometry;
	RayTracingInstance.InstanceTransforms.Add ( GetLocalToWorld ( ) );

	uint32     SectionIdx = 0;
	FMeshBatch MeshBatch;

	MeshBatch.VertexFactory       = &RenderBuffers.VertexFactory;
	MeshBatch.SegmentIndex        = 0;
	MeshBatch.MaterialRenderProxy = UseMaterialProxy;
	MeshBatch.Type                = PT_TriangleList;
	MeshBatch.DepthPriorityGroup  = DepthPriority;
	MeshBatch.CastRayTracedShadow = IsShadowCast ( FirstActiveView );

	FMeshBatchElement& BatchElement             = MeshBatch.Elements [ 0 ];
	BatchElement.IndexBuffer                    = &IndexBuffer;
	BatchElement.PrimitiveUniformBufferResource = &DynamicPrimitiveUniformBuffer.UniformBuffer;
	BatchElement.FirstIndex                     = 0;
	BatchElement.NumPrimitives                  = IndexBuffer.Indices.Num ( ) / 3;
	BatchElement.MinVertexIndex                 = 0;
	BatchElement.MaxVertexIndex                 = RenderBuffers.PositionVertexBuffer.GetNumVertices ( ) - 1;

	RayTracingInstance.Materials.Add ( MeshBatch );

	for ( int32 ViewIndex = 0 ; ViewIndex < Views.Num ( ) ; ViewIndex++ )
	{
		if ( ( VisibilityMap & ( 1 << ViewIndex ) ) == 0 )
		{
			continue;
		}

		Collector.AddRayTracingInstance ( ViewIndex , RayTracingInstance );
	}
}

#endif // RHI_RAYTRACING

// Other

FPrimitiveViewRelevance FLPPChunkedDynamicMeshProxy::GetViewRelevance ( const FSceneView* View ) const
{
	FPrimitiveViewRelevance Result;

	Result.bDrawRelevance   = IsShown ( View );
	Result.bShadowRelevance = IsShadowCast ( View );

	bool bUseStaticDrawPath  = bPreferStaticDrawPath && AllowStaticDrawPath ( View );
	Result.bDynamicRelevance = !bUseStaticDrawPath;
	Result.bStaticRelevance  = bUseStaticDrawPath;
#if WITH_EDITOR
	//only check these in the editor
	Result.bEditorVisualizeLevelInstanceRelevance = IsEditingLevelInstanceChild ( );
	Result.bEditorStaticSelectionRelevance        = ( IsSelected ( ) || IsHovered ( ) );
#endif


	Result.bRenderInMainPass      = ShouldRenderInMainPass ( );
	Result.bUsesLightingChannels  = GetLightingChannelMask ( ) != GetDefaultLightingChannelMask ( );
	Result.bTranslucentSelfShadow = bCastVolumetricTranslucentShadow;
	Result.bRenderCustomDepth     = ShouldRenderCustomDepth ( );
	// Note that this is actually a getter. One may argue that it is poorly named.
	MaterialRelevance.SetPrimitiveViewRelevance ( Result );
	Result.bVelocityRelevance = DrawsVelocity ( ) && Result.bOpaque && Result.bRenderInMainPass;

	return Result;
}

void FLPPChunkedDynamicMeshProxy::GetLightRelevance ( const FLightSceneProxy* LightSceneProxy , bool& bDynamic , bool& bRelevant , bool& bLightMapped , bool& bShadowMapped ) const
{
	FPrimitiveSceneProxy::GetLightRelevance ( LightSceneProxy , bDynamic , bRelevant , bLightMapped , bShadowMapped );
}

bool FLPPChunkedDynamicMeshProxy::CanBeOccluded ( ) const
{
	return !MaterialRelevance.bDisableDepthTest;
}

uint32 FLPPChunkedDynamicMeshProxy::GetMemoryFootprint ( ) const
{
	return ( sizeof( *this ) + GetAllocatedSize ( ) );
}

uint32 FLPPChunkedDynamicMeshProxy::GetAllocatedSize ( ) const
{
	return ( FPrimitiveSceneProxy::GetAllocatedSize ( ) );
}

SIZE_T FLPPChunkedDynamicMeshProxy::GetTypeHash ( ) const
{
	static size_t UniquePointer;
	return reinterpret_cast < size_t > ( &UniquePointer );
}
