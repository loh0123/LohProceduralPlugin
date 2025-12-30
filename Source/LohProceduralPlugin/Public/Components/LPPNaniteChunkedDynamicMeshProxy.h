// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "LPPDynamicMesh.h"
#include "NaniteSceneProxy.h"
#include "Rendering/NaniteResources.h"

/**
 * 
 */
class FLPPNaniteChunkedDynamicMeshProxy final : public Nanite::FSceneProxyBase
{
public:

	FLPPNaniteChunkedDynamicMeshProxy ( const ULPPDynamicMesh* Component );

	virtual ~FLPPNaniteChunkedDynamicMeshProxy ( ) override
	{
#if RHI_RAYTRACING
		//ReleaseDynamicRayTracingGeometries ( );
#endif
	};

public:

	// FPrimitiveSceneProxy interface.
	virtual SIZE_T                  GetTypeHash ( ) const override;
	virtual FPrimitiveViewRelevance GetViewRelevance ( const FSceneView* View ) const override;
	virtual bool                    CanApplyStreamableRenderAssetScaleFactor ( ) const override { return true; }

	virtual void DrawStaticElements ( FStaticPrimitiveDrawInterface* PDI ) override;

	// TODO : Collision
	//virtual void GetDynamicMeshElements ( const TArray < const FSceneView* >& Views , const FSceneViewFamily& ViewFamily , uint32 VisibilityMap , FMeshElementCollector& Collector ) const override;

	virtual void GetPreSkinnedLocalBounds ( FBoxSphereBounds& OutBounds ) const override;

	//#if NANITE_ENABLE_DEBUG_RENDERING
	//	/** Sets up a collision FMeshBatch for a specific LOD and element. */
	//	virtual bool GetCollisionMeshElement (
	//		int32                       LODIndex ,
	//		int32                       BatchIndex ,
	//		int32                       ElementIndex ,
	//		uint8                       InDepthPriorityGroup ,
	//		const FMaterialRenderProxy* RenderProxy ,
	//		FMeshBatch&                 OutMeshBatch ) const;
	//#endif

#if RHI_RAYTRACING
	//virtual bool                              HasRayTracingRepresentation ( ) const override;
	//virtual bool                              IsRayTracingRelevant ( ) const override { return true; }
	//virtual bool                              IsRayTracingStaticRelevant ( ) const override { return !bDynamicRayTracingGeometry; }
	//virtual void                              GetDynamicRayTracingInstances ( FRayTracingInstanceCollector& Collector ) override;
	//virtual ERayTracingPrimitiveFlags         GetCachedRayTracingInstance ( FRayTracingInstance& RayTracingInstance ) override;
	//virtual Nanite::CoarseMeshStreamingHandle GetCoarseMeshStreamingHandle ( ) const override { return CoarseMeshStreamingHandle; }
	//virtual RayTracing::FGeometryGroupHandle  GetRayTracingGeometryGroupHandle ( ) const override;
#endif

	virtual uint32 GetMemoryFootprint ( ) const override;

	virtual void GetDistanceFieldAtlasData ( const FDistanceFieldVolumeData*& OutDistanceFieldData , float& SelfShadowBias ) const override;
	virtual bool HasDistanceFieldRepresentation ( ) const override;

	virtual const FCardRepresentationData* GetMeshCardRepresentation ( ) const override;

	virtual Nanite::FResourceMeshInfo      GetResourceMeshInfo ( ) const override;
	virtual Nanite::FResourcePrimitiveInfo GetResourcePrimitiveInfo ( ) const override;

	//virtual void SetEvaluateWorldPositionOffsetInRayTracing ( FRHICommandListBase& RHICmdList , bool bEvaluate );

protected:

	virtual void CreateRenderThreadResources ( FRHICommandListBase& RHICmdList ) override;

	virtual void OnEvaluateWorldPositionOffsetChanged_RenderThread ( ) override;

	bool IsCollisionView ( const FEngineShowFlags& EngineShowFlags , bool& bDrawSimpleCollision , bool& bDrawComplexCollision ) const;

#if RHI_RAYTRACING
	//int32        GetFirstValidRaytracingGeometryLODIndex ( ERayTracingMode RayTracingMode , bool bForDynamicUpdate = false ) const;
	//virtual void SetupFallbackRayTracingMaterials ( int32 LODIndex , TArray < FMeshBatch >& OutMaterials ) const;
	//void         GetDynamicRayTracingInstances_Internal ( FRayTracingInstanceCollector& Collector , FRWBuffer* DynamicVertexBuffer = nullptr , bool bUpdateRayTracingGeometry = true );
#endif // RHI_RAYTRACING

#if RHI_RAYTRACING || NANITE_ENABLE_DEBUG_RENDERING
	//bool IsReversedCullingNeeded ( bool bUseReversedIndices ) const;
#endif

protected:

	const Nanite::FResources* Resources = nullptr;

	uint32 bHasMaterialErrors : 1;

	/** Untransformed bounds of the mesh */
	FBoxSphereBounds MeshBounds;

	uint32 MinDrawDistance = 0;
	uint32 EndCullDistance = 0;

#if RHI_RAYTRACING
	//void CreateDynamicRayTracingGeometries ( FRHICommandListBase& RHICmdList );
	//void ReleaseDynamicRayTracingGeometries ( );
	//
	//TArray < FRayTracingGeometry , TInlineAllocator < MAX_MESH_LOD_COUNT > > DynamicRayTracingGeometries;
	//Nanite::CoarseMeshStreamingHandle                                        CoarseMeshStreamingHandle = INDEX_NONE;
	//TArray < FMeshBatch >                                                    CachedRayTracingMaterials;
	//int16                                                                    CachedRayTracingMaterialsLODIndex = INDEX_NONE;
	//
	//bool bSupportRayTracing           : 1 = false;
	//bool bHasRayTracingRepresentation : 1 = false;
	//bool bDynamicRayTracingGeometry   : 1 = false;
	//
	//RayTracing::FGeometryGroupHandle RayTracingGeometryGroupHandle = INDEX_NONE;

#endif

#if NANITE_ENABLE_DEBUG_RENDERING
	UObject* Owner;

	/** Body setup for collision debug rendering */
	UBodySetup* BodySetup;

	/** Collision trace flags */
	ECollisionTraceFlag CollisionTraceFlag;

	/** Collision Response of this component */
	FCollisionResponseContainer CollisionResponse;

	/** Draw mesh collision if used for complex collision */
	uint32 bDrawMeshCollisionIfComplex : 1;

	/** Draw mesh collision if used for simple collision */
	uint32 bDrawMeshCollisionIfSimple : 1;
#endif

#if RHI_RAYTRACING || NANITE_ENABLE_DEBUG_RENDERING
	//	
	//	class FFallbackLODInfo
	//	{
	//	public:
	//
	//		/** Information about an element of a LOD. */
	//		struct FSectionInfo
	//		{
	//			/** Default constructor. */
	//			FSectionInfo ( ) : MaterialProxy ( nullptr )
	//#if WITH_EDITOR
	//			                   , bSelected ( false )
	//			                   , HitProxy ( nullptr )
	//#endif
	//			{
	//			}
	//
	//			/** The material with which to render this section. */
	//			FMaterialRenderProxy* MaterialProxy;
	//
	//#if WITH_EDITOR
	//			/** True if this section should be rendered as selected (editor only). */
	//			bool bSelected;
	//
	//			/** The editor needs to be able to individual sub-mesh hit detection, so we store a hit proxy on each mesh. */
	//			HHitProxy* HitProxy;
	//#endif
	//
	//#if WITH_EDITORONLY_DATA
	//			// The material index from the component. Used by the texture streaming accuracy viewmodes.
	//			int32 MaterialIndex;
	//#endif
	//		};
	//
	//		/** Per-section information. */
	//		TArray < FSectionInfo , TInlineAllocator < 1 > > Sections;
	//
	//		/** Vertex color data for this LOD (or NULL when not overridden), FStaticMeshComponentLODInfo handles the release of the memory */
	//		FColorVertexBuffer* OverrideColorVertexBuffer;
	//
	//		TUniformBufferRef < FLocalVertexFactoryUniformShaderParameters > OverrideColorVFUniformBuffer;
	//
	//		FFallbackLODInfo (
	//			const FStaticMeshSceneProxyDesc&  InProxyDesc ,
	//			const FStaticMeshVertexBuffers&   InVertexBuffers ,
	//			const FStaticMeshSectionArray&    InSections ,
	//			const FStaticMeshVertexFactories& InVertexFactories ,
	//			int32                             InLODIndex ,
	//			int32                             InClampedMinLOD
	//			);
	//	};
	//
	//	/** Configures mesh batch vertex / index state. Returns the number of primitives used in the element. */
	//	uint32 SetMeshElementGeometrySource (
	//		const FStaticMeshSection&                Section ,
	//		const FFallbackLODInfo::FSectionInfo&    SectionInfo ,
	//		const FRawStaticIndexBuffer&             IndexBuffer ,
	//		const FAdditionalStaticMeshIndexBuffers* AdditionalIndexBuffers ,
	//		const ::FVertexFactory*                  VertexFactory ,
	//		bool                                     bWireframe ,
	//		bool                                     bUseReversedIndices ,
	//		FMeshBatch&                              OutMeshElement ) const;
	//
	//	
#endif

#if RHI_RAYTRACING
	//TArray < FFallbackLODInfo > RayTracingFallbackLODs;
#endif

#if NANITE_ENABLE_DEBUG_RENDERING
	//TArray < FFallbackLODInfo > FallbackLODs;
#endif

private:

	TSharedPtr < FLPPDynamicMeshRenderData > MeshRenderData;

	TSharedPtr < FCardRepresentationData > LumenCardData = nullptr;

	TSharedPtr < FDistanceFieldVolumeData > DistanceFieldPtr = nullptr;

	float DFBias = 0.0f;
};
