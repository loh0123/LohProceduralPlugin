// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "LPPChunkedDynamicMeshProxy.h"
#include "PhysicsEngine/AggregateGeom.h"

#include "LPPDynamicMesh.generated.h"

LLM_DECLARE_TAG ( LFPDynamicMesh );

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

	float GetDistanceFieldSelfShadowBias ( ) const;

public:

	//~ Begin UPrimitiveComponent Interface.
	FLPPChunkedDynamicMeshProxy* GetBaseSceneProxy ( ) const;

	//~ Begin UPrimitiveComponent Interface.
	virtual FPrimitiveSceneProxy* CreateSceneProxy ( ) override;

	//~ USceneComponent Interface.
	virtual FBoxSphereBounds CalcBounds ( const FTransform& LocalToWorld ) const override;

public:

	const TSharedPtr < FLPPDynamicMeshRenderData >& GetRenderData ( ) const;

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
