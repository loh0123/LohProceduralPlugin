// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "LPPChunkedDynamicMeshProxy.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "PhysicsEngine/AggregateGeom.h"

#include "LPPChunkedDynamicMesh.generated.h"

USTRUCT ( )
struct FLPPChunkedDynamicCompactMeshData
{
	GENERATED_BODY ( )

	FLPPChunkedDynamicCompactMeshData ( ) = default;

	FLPPChunkedDynamicCompactMeshData ( const int32 InNumVertices , const bool bHasColor )
	{
		Position.AddUninitialized ( InNumVertices );
		Normal.AddUninitialized ( InNumVertices );
		UV0.AddUninitialized ( InNumVertices );

		if ( bHasColor )
		{
			Color.AddUninitialized ( InNumVertices );
		}
	}

public:

	/** Vertex position */
	UPROPERTY ( )
	TArray < FVector3f > Position = TArray < FVector3f > ( );

	/** Vertex normal */
	UPROPERTY ( )
	TArray < FVector3f > Normal = TArray < FVector3f > ( );

	/** Vertex color */
	UPROPERTY ( )
	TArray < FColor > Color = TArray < FColor > ( );

	/** Vertex texture co-ordinate */
	UPROPERTY ( )
	TArray < FVector2f > UV0 = TArray < FVector2f > ( );
};

UCLASS ( ClassGroup=(Custom) , meta=(BlueprintSpawnableComponent) )
class LOHPROCEDURALPLUGIN_API ULPPChunkedDynamicMesh : public UMeshComponent , public IInterface_CollisionDataProvider
{
	GENERATED_BODY ( )

public:

	// Sets default values for this component's properties
	ULPPChunkedDynamicMesh ( );

protected:

	// Called when the game starts
	virtual void BeginPlay ( ) override;

	virtual void EndPlay ( const EEndPlayReason::Type EndPlayReason ) override;

public:

	// Called every frame
	virtual void TickComponent ( float DeltaTime , ELevelTick TickType , FActorComponentTickFunction* ThisTickFunction ) override;

public:

	virtual void SetMesh ( FDynamicMesh3&& MoveMesh );

	virtual void ClearMesh ( );

public:

	virtual void NotifyMeshUpdated ( const FDynamicMesh3& MeshData );
	virtual void NotifyMaterialSetUpdated ( );

protected:

	UPROPERTY ( Transient )
	FKAggregateGeom AggGeom;

	UPROPERTY ( Transient )
	TObjectPtr < UBodySetup > MeshBodySetup;

protected:

	UPROPERTY ( EditAnywhere , Category = "Setting" )
	bool bEnableComplexCollision = false;

protected:

	/** Current local-space bounding box of Mesh */
	UE::Geometry::FAxisAlignedBox3d LocalBounds;

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
	virtual FMaterialRelevance  GetMaterialRelevance ( ERHIFeatureLevel::Type InFeatureLevel ) const override;
	virtual void                SetMaterial ( int32 ElementIndex , UMaterialInterface* Material ) override;
	virtual void                GetUsedMaterials ( TArray < UMaterialInterface* >& OutMaterials , bool bGetDebugMaterials = false ) const override;

	virtual void SetNumMaterials ( int32 NumMaterials );

	UPROPERTY ( )
	TArray < TObjectPtr < UMaterialInterface > > BaseMaterials;

public:

	//~ Begin UPrimitiveComponent Interface.
	FLPPChunkedDynamicMeshProxy* GetBaseSceneProxy ( ) const;

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy ( ) override;

	//~ USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds ( const FTransform& LocalToWorld ) const override;

protected:

	UPROPERTY ( )
	FLPPChunkedDynamicCompactMeshData MeshCompactData;
};
