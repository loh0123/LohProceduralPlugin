// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "MeshCardBuild.h"
#include "RHICommandList.h"
#include "RayTracingGeometry.h"
#include "DistanceFieldAtlas.h"
#include "Templates/PimplPtr.h"
#include "PrimitiveViewRelevance.h"
#include "Components/BaseDynamicMeshSceneProxy.h"

/**
 * 
 */
class FLPPChunkedDynamicMeshProxy final : public FPrimitiveSceneProxy
{
	using FIndex2i = UE::Geometry::FIndex2i;
	using FIndex3i = UE::Geometry::FIndex3i;

public:

	FLPPChunkedDynamicMeshProxy ( UMeshComponent* Component , const float NewDFBias ) : FPrimitiveSceneProxy ( Component )
	                                                                                    , bEnableRaytracing ( true )
	                                                                                    , bPreferStaticDrawPath ( true )
	                                                                                    , DistanceFieldData ( nullptr )
	                                                                                    , MaterialRelevance ( Component->GetMaterialRelevance ( GetScene ( ).GetFeatureLevel ( ) ) )
	                                                                                    , ParentComponent ( Component )
	                                                                                    , DFBias ( NewDFBias )
	                                                                                    , CollisionTraceFlag ( )
	{
		SetCollisionData ( );

		bOpaqueOrMasked = MaterialRelevance.bOpaque;

		bVerifyUsedMaterials = true;

		bHasDeformableMesh                                  = false;
		bStaticElementsAlwaysUseProxyPrimitiveUniformBuffer = true;
		bSupportsSortedTriangles                            = true;
	}

	virtual ~FLPPChunkedDynamicMeshProxy ( ) override
	{
		for ( FMeshRenderBufferSet* BufferSet : AllocatedBufferSets )
		{
			FMeshRenderBufferSet::DestroyRenderBufferSet ( BufferSet );
		}
	}

protected:

	// Set of currently-allocated RenderBuffers. We own these pointers and must clean them up.
	// Must guard access with AllocatedSetsLock!!
	TArray < FMeshRenderBufferSet* > AllocatedBufferSets;

	// use to control access to AllocatedBufferSets 
	FCriticalSection AllocatedSetsLock;

	// control raytracing support
	bool bEnableRaytracing = false;

	// whether to try to use the static draw instead of dynamic draw path; note we may still use the dynamic path if collision or vertex color rendering is enabled
	bool bPreferStaticDrawPath = false;

protected:

	TPimplPtr < FCardRepresentationData > MeshCards;

	bool bMeshCardsValid = false;

protected:

	TSharedPtr < FDistanceFieldVolumeData > DistanceFieldData;

private:

	FMaterialRelevance MaterialRelevance;

public:

	/** Component that created this proxy (is there a way to look this up?) */
	UMeshComponent* ParentComponent;

	float DFBias = 0.0f;

#if UE_ENABLE_DEBUG_DRAWING

private:

	// If debug drawing is enabled, we store collision data here so that collision shapes can be rendered when requested by showflags

	bool bOwnerIsNull = true;
	/** Whether the collision data has been set up for rendering */
	bool bHasCollisionData = false;
	/** Whether a complex collision mesh is available */
	bool bHasComplexMeshData = false;
	/** Collision trace flags */
	ECollisionTraceFlag CollisionTraceFlag;
	/** Collision Response of this component */
	FCollisionResponseContainer CollisionResponse;
	/** Cached AggGeom holding the collision shapes to render */
	FKAggregateGeom CachedAggGeom;

	// Control access to collision data for debug rendering
	mutable FCriticalSection CachedCollisionLock;

#endif

public: // Render Buffer

	FMeshRenderBufferSet* AllocateNewRenderBufferSet ( );

	void ReleaseRenderBufferSet ( FMeshRenderBufferSet* BufferSet );

	//void GetActiveRenderBufferSets ( TArray < FMeshRenderBufferSet* >& Buffers ) const;

public:

	LOHPROCEDURALPLUGIN_API void Initialize ( const TFunctionRef < bool  (
		TArray < FVector3f >& PositionList ,
		TArray < uint32 >&    IndexList ,
		TArray < FVector2f >& UV0List ,
		TArray < FColor >&    ColorList ,
		TArray < FVector3f >& NormalList ,
		TArray < FVector3f >& TangentList ,
		TArray < FVector3f >& BiTangentList
		) >& InitFunc );

public:

	LOHPROCEDURALPLUGIN_API void SetDistanceFieldData ( const TSharedPtr < FDistanceFieldVolumeData >& InDistanceFieldData );

	LOHPROCEDURALPLUGIN_API void SetLumenData ( const TArray < class FLumenCardBuildData >& InLumenCardData , const FBox& InLumenBound );

public:

	virtual void GetDistanceFieldAtlasData ( const class FDistanceFieldVolumeData*& OutDistanceFieldData , float& SelfShadowBias ) const override;

	virtual bool HasDistanceFieldRepresentation ( ) const override;

	virtual bool HasDynamicIndirectShadowCasterRepresentation ( ) const override;

	virtual const FCardRepresentationData* GetMeshCardRepresentation ( ) const override;

protected:

	static FMaterialRenderProxy* GetEngineVertexColorMaterialProxy ( FMeshElementCollector& Collector , const FEngineShowFlags& EngineShowFlags , bool bProxyIsSelected , bool bIsHovered );

	virtual void GetDynamicMeshElements (
		const TArray < const FSceneView* >& Views ,
		const FSceneViewFamily&             ViewFamily ,
		uint32                              VisibilityMap ,
		FMeshElementCollector&              Collector ) const override;

	void GetCollisionDynamicMeshElements ( TArray < FMeshRenderBufferSet* >&   Buffers ,
	                                       const FEngineShowFlags&             EngineShowFlags , bool bDrawCollisionView , bool bDrawSimpleCollision , bool bDrawComplexCollision ,
	                                       bool                                bProxyIsSelected ,
	                                       const TArray < const FSceneView* >& Views , uint32 VisibilityMap ,
	                                       FMeshElementCollector&              Collector ) const;

public:

	virtual void DrawStaticElements ( FStaticPrimitiveDrawInterface* PDI ) override;

	bool AllowStaticDrawPath ( const FSceneView* View ) const;

	void DrawBatch ( FMeshElementCollector&           Collector ,
	                 const FMeshRenderBufferSet&      RenderBuffers ,
	                 const FDynamicMeshIndexBuffer32& IndexBuffer ,
	                 FMaterialRenderProxy*            UseMaterial ,
	                 bool                             bWireframe ,
	                 ESceneDepthPriorityGroup         DepthPriority ,
	                 int                              ViewIndex ,
	                 FDynamicPrimitiveUniformBuffer&  DynamicPrimitiveUniformBuffer ) const;

#if RHI_RAYTRACING

	virtual bool IsRayTracingRelevant ( ) const override;
	virtual bool HasRayTracingRepresentation ( ) const override;

	virtual void GetDynamicRayTracingInstances ( FRayTracingInstanceCollector& Collector ) override;


	/**
	* Draw a single-frame raytracing FMeshBatch for a FMeshRenderBufferSet
	*/
	void DrawRayTracingBatch (
		FRayTracingInstanceCollector&    Collector ,
		const FMeshRenderBufferSet&      RenderBuffers ,
		const FDynamicMeshIndexBuffer32& IndexBuffer ,
		FRayTracingGeometry&             RayTracingGeometry ,
		FMaterialRenderProxy*            UseMaterialProxy ,
		ESceneDepthPriorityGroup         DepthPriority ,
		FDynamicPrimitiveUniformBuffer&  DynamicPrimitiveUniformBuffer ) const;


#endif // RHI_RAYTRACING

public:

	virtual FPrimitiveViewRelevance GetViewRelevance ( const FSceneView* View ) const override;

	void UpdatedReferencedMaterials ( );

public:

	void SetCollisionData ( );

private:

	bool IsCollisionView ( const FEngineShowFlags& EngineShowFlags , bool& bDrawSimpleCollision , bool& bDrawComplexCollision ) const;

public:

	virtual void GetLightRelevance ( const FLightSceneProxy* LightSceneProxy , bool& bDynamic , bool& bRelevant , bool& bLightMapped , bool& bShadowMapped ) const override;

	virtual bool CanBeOccluded ( ) const override;

	virtual uint32 GetMemoryFootprint ( void ) const override;

	uint32 GetAllocatedSize ( void ) const;

	virtual SIZE_T GetTypeHash ( ) const override;
};
