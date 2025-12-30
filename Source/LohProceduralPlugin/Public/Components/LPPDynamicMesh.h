// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "LPPChunkedDynamicMeshProxy.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PhysicsEngine/AggregateGeom.h"

#include "LPPDynamicMesh.generated.h"

LLM_DECLARE_TAG ( LFPDynamicMesh );

class LOHPROCEDURALPLUGIN_API FLPPDynamicMeshRenderData
{
public:

	FLPPDynamicMeshRenderData ( FLPPDynamicMeshRenderData&& Other ) noexcept : MaterialMapping ( MoveTemp ( Other.MaterialMapping ) ),
	                                                                           MeshData ( MoveTemp ( Other.MeshData ) ),
	                                                                           LocalBounds ( MoveTemp ( Other.LocalBounds ) ),
	                                                                           Bounds ( MoveTemp ( Other.Bounds ) ),
	                                                                           bHasNaniteFallbackMesh ( Other.bHasNaniteFallbackMesh ),
	                                                                           bReadyForStreaming ( Other.bReadyForStreaming ),
	                                                                           bIsInitialized ( Other.bIsInitialized )
	{
		if ( Other.NaniteResourcesPtr.IsValid ( ) )
		{
			NaniteResourcesPtr = MoveTemp ( Other.NaniteResourcesPtr );
		}

		if ( Other.LumenCardData.IsValid ( ) )
		{
			LumenCardData = MoveTemp ( Other.LumenCardData );
		}

		if ( Other.DistanceFieldPtr.IsValid ( ) )
		{
			DistanceFieldPtr = MoveTemp ( Other.DistanceFieldPtr );
		}
	}

	/** Default constructor. */
	FLPPDynamicMeshRenderData ( );

	~FLPPDynamicMeshRenderData ( );

	/**
	 * Per-LOD resources. For compatibility reasons, the FStaticMeshLODResources array are not referenced through TRefCountPtr.
	 * The LODResource still has a ref count of at least 1, see FStaticMeshLODResources() constructor.
	 */
	//FStaticMeshLODResourcesArray    LODResources;
	//FStaticMeshVertexFactoriesArray LODVertexFactories;

	/** Screen size to switch LODs */
	//FPerPlatformFloat ScreenSize [ MAX_STATIC_MESH_LODS ];

	TArray < uint32 > MaterialMapping;

	FDynamicMesh3 MeshData = FDynamicMesh3 ( );

	TPimplPtr < Nanite::FResources > NaniteResourcesPtr = nullptr;

	TSharedPtr < FCardRepresentationData > LumenCardData = nullptr;

	TSharedPtr < FDistanceFieldVolumeData > DistanceFieldPtr = nullptr;

	/** Ray tracing representation of this mesh, null if not present.  */
	//FStaticMeshRayTracingProxy* RayTracingProxy = nullptr;

	// Local Bounding Box
	UE::Geometry::FAxisAlignedBox3d LocalBounds;

	/** Bounds of the renderable mesh. */
	FBoxSphereBounds Bounds;

#if RHI_RAYTRACING
	//RayTracing::FGeometryGroupHandle RayTracingGeometryGroupHandle = INDEX_NONE;
#endif

	bool IsInitialized ( ) const
	{
		return bIsInitialized;
	}

	bool HasValidNaniteData ( ) const;

	bool HasNaniteFallbackMesh ( EShaderPlatform ShaderPlatform ) const;

	bool bHasNaniteFallbackMesh;

	/** True if rhi resources are initialized */
	bool bReadyForStreaming;


	/** Initialize the render resources. */
	void InitResources ( ERHIFeatureLevel::Type InFeatureLevel , ULPPDynamicMesh* Owner );

	/** Releases the render resources. */
	void ReleaseResources ( );

	/** Compute the size of this resource. */
	//void GetResourceSizeEx ( FResourceSizeEx& CumulativeResourceSize ) const;

	/** Get the estimated memory overhead of buffers marked as NeedsCPUAccess. */
	//SIZE_T GetCPUAccessMemoryOverhead ( ) const;

	// void InitializeRayTracingRepresentationFromRenderingLODs ( );

private:

	bool bIsInitialized = false;

public:

	FORCEINLINE FBoxSphereBounds CalcBoundByMeshData ( const FTransform& LocalToWorld , const float BoundsScale )
	{
		FBox LocalBoundingBox = static_cast < FBox > ( LocalBounds );
		Bounds                = LocalBoundingBox.TransformBy ( LocalToWorld );
		Bounds.BoxExtent      *= BoundsScale;
		Bounds.SphereRadius   *= BoundsScale;

		return Bounds;
	}
};

UCLASS ( ClassGroup=(Custom) , meta=(BlueprintSpawnableComponent) )
class LOHPROCEDURALPLUGIN_API ULPPDynamicMesh : public UMeshComponent , public IInterface_CollisionDataProvider
{
	GENERATED_BODY ( )

public:

	// Sets default values for this component's properties
	ULPPDynamicMesh ( );

protected:

	// Called when the game starts
	virtual void BeginPlay ( ) override;

	virtual void EndPlay ( const EEndPlayReason::Type EndPlayReason ) override;

public:

	// Called every frame
	virtual void TickComponent ( float DeltaTime , ELevelTick TickType , FActorComponentTickFunction* ThisTickFunction ) override;

public:

	virtual void SetMesh ( FLPPDynamicMeshRenderData&& MoveData , FKAggregateGeom&& NewAggGeom );
	virtual void ClearMesh ( );

public:

	virtual void NotifyMeshUpdated ( );
	virtual void NotifyMaterialSetUpdated ( );

protected:

	UPROPERTY ( Transient )
	uint16 SetMeshCounter = 0;

	UPROPERTY ( Transient )
	uint16 ClearMeshCounter = 0;

protected:

	UPROPERTY ( Transient )
	TObjectPtr < UBodySetup > MeshBodySetup = nullptr;

	UPROPERTY ( EditAnywhere , Category = "Setting" )
	bool bEnableComplexCollision = false;

public:

	virtual bool GetPhysicsTriMeshData ( struct FTriMeshCollisionData* CollisionData , bool InUseAllTriData ) override;
	virtual bool GetTriMeshSizeEstimates ( struct FTriMeshCollisionDataEstimates& OutTriMeshEstimates , bool bInUseAllTriData ) const override;
	virtual bool ContainsPhysicsTriMeshData ( bool InUseAllTriData ) const override;
	virtual bool WantsNegXTriMesh ( ) override;

	virtual UBodySetup* GetBodySetup ( ) override;

protected:

	void InvalidatePhysicsData ( );
	void RebuildPhysicsData ( );
	void FinishPhysicsAsyncCook ( bool bSuccess , UBodySetup* FinishedBodySetup );

	virtual UBodySetup* CreateBodySetupHelper ( );

public:

	// UMeshComponent Interface.
	virtual int32               GetNumMaterials ( ) const override;
	virtual UMaterialInterface* GetMaterial ( int32 ElementIndex ) const override;
	virtual FMaterialRelevance  GetMaterialRelevance ( EShaderPlatform InShaderPlatform ) const override;
	virtual void                SetMaterial ( int32 ElementIndex , UMaterialInterface* Material ) override;
	virtual void                GetUsedMaterials ( TArray < UMaterialInterface* >& OutMaterials , bool bGetDebugMaterials = false ) const override;

	virtual void SetNumMaterials ( int32 NumMaterials );

	UPROPERTY ( )
	TArray < TObjectPtr < UMaterialInterface > > BaseMaterials;

public:

	FORCEINLINE float GetDistanceFieldSelfShadowBias ( ) const;

public:

	//~ Begin UPrimitiveComponent Interface.
	FLPPChunkedDynamicMeshProxy* GetBaseSceneProxy ( ) const;

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy ( ) override;

	//~ USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds ( const FTransform& LocalToWorld ) const override;

public:

	FORCEINLINE const TSharedPtr < FLPPDynamicMeshRenderData >& GetRenderData ( ) const;

protected: // Committed Data

	FCriticalSection RenderDataLock;

	//UPROPERTY ( Transient )
	TSharedPtr < FLPPDynamicMeshRenderData > MeshRenderData = nullptr;

	UPROPERTY ( Transient )
	FKAggregateGeom AggGeom = FKAggregateGeom ( );

public:

	FRenderCommandFence ReleaseResourcesFence;

protected:

	UPROPERTY ( EditAnywhere , Category = "Setting" )
	float DistanceFieldSelfShadowBias = 1.0f;
};
