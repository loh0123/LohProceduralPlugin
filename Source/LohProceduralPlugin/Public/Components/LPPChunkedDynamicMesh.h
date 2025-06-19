// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "Components/BaseDynamicMeshComponent.h"
#include "PhysicsEngine/AggregateGeom.h"

#include "LPPChunkedDynamicMesh.generated.h"


UCLASS ( ClassGroup=(Custom) , meta=(BlueprintSpawnableComponent) )
class LOHPROCEDURALPLUGIN_API ULPPChunkedDynamicMesh : public UBaseDynamicMeshComponent , public IInterface_CollisionDataProvider
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

	virtual void ProcessMesh ( TFunctionRef < void  ( const UE::Geometry::FDynamicMesh3& ) > ProcessFunc ) const override;

	virtual FDynamicMesh3*       GetMesh ( ) override { return MeshObject->GetMeshPtr ( ); }
	virtual const FDynamicMesh3* GetMesh ( ) const override { return MeshObject->GetMeshPtr ( ); }
	virtual UDynamicMesh*        GetDynamicMesh ( ) override { return MeshObject; }

	virtual void SetMesh ( UE::Geometry::FDynamicMesh3&& MoveMesh ) override;
	virtual void ApplyTransform ( const FTransform3d& Transform , bool bInvert ) override;

	virtual void ClearMesh ( );

public:

	virtual void ApplyChange ( const FMeshVertexChange* Change , bool bRevert ) override;
	virtual void ApplyChange ( const FMeshChange* Change , bool bRevert ) override;
	virtual void ApplyChange ( const FMeshReplacementChange* Change , bool bRevert ) override;

public:

	virtual void NotifyMeshUpdated ( ) override;

protected:

	UPROPERTY ( Transient )
	FKAggregateGeom AggGeom;

	UPROPERTY ( Transient )
	TObjectPtr < UBodySetup > MeshBodySetup;

protected:

	/** Current local-space bounding box of Mesh */
	UE::Geometry::FAxisAlignedBox3d LocalBounds;

	/** Recompute LocalBounds from the current Mesh */
	void UpdateLocalBounds ( );

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

	virtual FBaseDynamicMeshSceneProxy* GetBaseSceneProxy ( ) override;

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy ( ) override;

	//~ USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds ( const FTransform& LocalToWorld ) const override;

protected:

	UPROPERTY ( Instanced )
	TObjectPtr < UDynamicMesh > MeshObject;
};
