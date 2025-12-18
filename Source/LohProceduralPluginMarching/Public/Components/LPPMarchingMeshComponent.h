// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "MeshCardBuild.h"
#include "Components/BaseDynamicMeshComponent.h"
#include "Components/LPPChunkedDynamicMesh.h"
#include "Subsystem/LPPProceduralWorldSubsystem.h"
#include "LPPMarchingMeshComponent.generated.h"

class ULFPChunkedGridPositionComponent;
class ULFPChunkedTagDataComponent;
class ULPPMarchingData;

LLM_DECLARE_TAG ( LFPMarchingMesh );

struct FLFPMarchingRendererFaceDirection
{
	FLFPMarchingRendererFaceDirection ( FIntVector F , FIntVector R , FIntVector U ) : Forward ( F ), Right ( R ), Up ( U )
	{
	}

	FIntVector Forward , Right , Up = FIntVector::NoneValue;

public:

	FORCEINLINE void SetAxis ( FVector3f& X , FVector3f& Y , FVector3f& Z ) const
	{
		X = FVector3f ( Forward );
		Y = FVector3f ( Right );
		Z = FVector3f ( Up );
	}
};

namespace LFPMarchingRenderConstantData
{
	static const TArray < FRotator > VertexRotationList =
	{
		FRotator ( 0.0f , 0.0f , 0.0f ) , FRotator ( 90.0f , 0.0f , 0.0f ) , FRotator ( 90.0f , 270.0f , 0.0f ) , FRotator ( 180.0f , 0.0f , 0.0f ) , FRotator ( 90.0f , 180.0f , 0.0f ) , FRotator ( 90.0f , 90.0f , 0.0f )
	};

	static const TArray < FLFPMarchingRendererFaceDirection > FaceDirection = {
		FLFPMarchingRendererFaceDirection ( FIntVector ( 1 , 0 , 0 ) , FIntVector ( 0 , 1 , 0 ) , FIntVector ( 0 , 0 , 1 ) ) , FLFPMarchingRendererFaceDirection ( FIntVector ( 0 , 0 , 1 ) , FIntVector ( 0 , 1 , 0 ) , FIntVector ( -1 , 0 , 0 ) ) , FLFPMarchingRendererFaceDirection ( FIntVector ( 0 , 0 , 1 ) , FIntVector ( 1 , 0 , 0 ) , FIntVector ( 0 , 1 , 0 ) ) , FLFPMarchingRendererFaceDirection ( FIntVector ( -1 , 0 , 0 ) , FIntVector ( 0 , 1 , 0 ) , FIntVector ( 0 , 0 , -1 ) ) , FLFPMarchingRendererFaceDirection ( FIntVector ( 0 , 0 , 1 ) , FIntVector ( 0 , -1 , 0 ) , FIntVector ( 1 , 0 , 0 ) ) , FLFPMarchingRendererFaceDirection ( FIntVector ( 0 , 0 , 1 ) , FIntVector ( -1 , 0 , 0 ) , FIntVector ( 0 , -1 , 0 ) )
	};

	static const TArray < FIntVector > FaceLoopDirectionList = {
		FIntVector ( 0 , 1 , 2 ) , FIntVector ( 2 , 1 , 0 ) , FIntVector ( 2 , 0 , 1 ) , FIntVector ( 0 , 1 , 2 ) , FIntVector ( 2 , 1 , 0 ) , FIntVector ( 2 , 0 , 1 )
	};

	static const TArray < int32 > SurfaceDirectionID = {
		5 , 0 , 3 , 4 , 1 , 2
	};
};

USTRUCT ( )
struct FLFPMarchingPassData
{
	GENERATED_BODY ( )

public:

	bool bIsChunkFaceCullingDisable  = false;
	bool bIsRegionFaceCullingDisable = false;

public:

	bool       bRenderData       = false;
	FVector    MeshFullSize      = FVector ( );
	FIntVector DataSize          = FIntVector ( );
	float      BoundExpand       = 0.0f;
	float      EdgeMergeDistance = 0.1f;

public:

	bool  bSimplifyRenderData = false;
	float SimplifyAngle       = 0.1f;

public:

	bool bSimpleBoxCollisionData = false;

public:

	bool       bRecomputeBoxUV = false;
	FTransform UVBoxTransform  = FTransform ( );

public:

	FDateTime StartTime = FDateTime ( );

	uint16 DataID = 0;

public:

	UPROPERTY ( )
	TObjectPtr < ULPPMarchingData > RenderSetting = nullptr;
};

USTRUCT ( )
struct FLFPMarchingThreadData
{
	GENERATED_BODY ( )

public:

	uint16 DataID = 0;

	FDynamicMesh3 MeshData = FDynamicMesh3 ( UE::Geometry::EMeshComponents::FaceGroups );

	TArray < FLumenCardBuildData > LumenCardData = TArray < FLumenCardBuildData > ( );
	FBox                           LumenBound    = FBox ( );

	TArray < FKBoxElem > CollisionBoxElems;

	FDateTime StartTime  = FDateTime ( );
	uint32    WorkLenght = 0;

public:

	uint32 GetByteCount ( ) const
	{
		return sizeof ( FLFPMarchingThreadData ) + MeshData.GetByteCount ( ) + ( sizeof ( FLumenCardBuildData ) * LumenCardData.Num ( ) ) + ( sizeof ( FKBoxElem ) * CollisionBoxElems.Num ( ) );
	}
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam ( FLFPOnMarchingMeshGenerateEvent , USceneComponent* , Component );

UCLASS ( ClassGroup=(Custom) , meta=(BlueprintSpawnableComponent) )
class LOHPROCEDURALPLUGINMARCHING_API ULPPMarchingMeshComponent : public ULPPChunkedDynamicMesh
{
	GENERATED_BODY ( )

public:

	// Sets default values for this component's properties
	ULPPMarchingMeshComponent ( );

protected:

	// Called when the game starts
	virtual void BeginPlay ( ) override;

	virtual void EndPlay ( const EEndPlayReason::Type EndPlayReason ) override;

public:

	// Called every frame
	virtual void TickComponent ( float DeltaTime , ELevelTick TickType , FActorComponentTickFunction* ThisTickFunction ) override;

public:

	UPROPERTY ( BlueprintAssignable )
	FLFPOnMarchingMeshGenerateEvent OnMeshSkipOnEmpty;

	UPROPERTY ( BlueprintAssignable )
	FLFPOnMarchingMeshGenerateEvent OnMeshSkipOnFull;

	UPROPERTY ( BlueprintAssignable )
	FLFPOnMarchingMeshGenerateEvent OnMeshRebuilding;

	UPROPERTY ( BlueprintAssignable )
	FLFPOnMarchingMeshGenerateEvent OnMeshGenerated;

	UPROPERTY ( BlueprintAssignable )
	FLFPOnMarchingMeshGenerateEvent OnDistanceFieldRebuilding;

	UPROPERTY ( BlueprintAssignable )
	FLFPOnMarchingMeshGenerateEvent OnDistanceFieldGenerated;

protected:

	UPROPERTY ( EditDefaultsOnly , Category="Setting|Render" )
	bool bGenerateRenderData = false;

	UPROPERTY ( EditDefaultsOnly , Category="Setting|Render" )
	TObjectPtr < ULPPMarchingData > RenderSetting = nullptr;

	UPROPERTY ( EditDefaultsOnly , Category="Setting|Render" )
	FGameplayTag HandleTag = FGameplayTag::EmptyTag;

	UPROPERTY ( EditDefaultsOnly , Category="Setting|Render" )
	float EdgeMergeDistance = 2.0f;

	UPROPERTY ( EditDefaultsOnly , Category="Setting|Render" )
	float BoundExpand = 25.0f;


	UPROPERTY ( EditDefaultsOnly , Category="Setting|Simplify" )
	bool bSimplifyRenderData = false;

	UPROPERTY ( EditDefaultsOnly , Category="Setting|Simplify" )
	float SimplifyAngle = 2.0f;


	UPROPERTY ( EditDefaultsOnly , Category="Setting|UV" )
	bool bRecomputeBoxUV = false;

	UPROPERTY ( EditDefaultsOnly , Category="Setting|UV" )
	FTransform UVBoxTransform = FTransform ( );


	UPROPERTY ( EditDefaultsOnly , Category="Setting|Collision" )
	bool bGenerateSimpleBoxCollisionData = false;


	UPROPERTY ( EditDefaultsOnly , Category="Setting|DistanceField" )
	bool bGenerateDistanceField = false;

	UPROPERTY ( EditDefaultsOnly , Category="Setting|DistanceField" )
	float DistanceFieldResolutionScale = 1.0f;

	UPROPERTY ( EditDefaultsOnly , Category="Setting|DistanceField" )
	float DistanceFieldBatchTime = 1.0f;

protected:

	UPROPERTY ( Transient )
	TObjectPtr < ULFPChunkedTagDataComponent > DataComponent = nullptr;

	UPROPERTY ( Transient )
	TObjectPtr < ULFPChunkedGridPositionComponent > PositionComponent = nullptr;

	UPROPERTY ( Transient )
	int32 RegionIndex = INDEX_NONE;

	UPROPERTY ( Transient )
	int32 ChunkIndex = INDEX_NONE;

public:

	UFUNCTION ( BlueprintCallable , Category="LFPMarchingMeshComponent" )
	FORCEINLINE FIntVector GetDataSize ( ) const;

	UFUNCTION ( BlueprintCallable , Category="LFPMarchingMeshComponent" )
	FORCEINLINE int32 GetDataNum ( ) const;

	UFUNCTION ( BlueprintCallable , Category="LFPMarchingMeshComponent" )
	FORCEINLINE FVector GetMeshSize ( ) const;

	UFUNCTION ( BlueprintCallable , Category="LFPMarchingMeshComponent" )
	FORCEINLINE bool IsDataComponentValid ( ) const;

	UFUNCTION ( BlueprintCallable , Category="LFPMarchingMeshComponent" )
	FORCEINLINE void GetFaceCullingSetting ( bool& bIsChunkFaceCullingDisable , bool& bIsRegionFaceCullingDisable ) const;

protected:

	UFUNCTION ( )
	FORCEINLINE uint8 GetMarchingID ( const FIntVector& Offset ) const;

public:

	UFUNCTION ( BlueprintCallable , Category="LFPVoxelRender" )
	void Initialize ( ULFPChunkedTagDataComponent* NewDataComponent , ULFPChunkedGridPositionComponent* NewPositionComponent , const int32 NewRegionIndex , const int32 NewChunkIndex );

	UFUNCTION ( BlueprintCallable , Category="LFPVoxelRender" )
	void Uninitialize ( );

public:

	UFUNCTION ( BlueprintCallable , Category = "LFPVoxelRender" )
	void ClearRender ( );

	UFUNCTION ( BlueprintCallable , Category = "LFPVoxelRender" )
	bool UpdateRender ( );

protected:

	// Add Mesh Fill
	void UpdateDistanceField ( );

protected:

	virtual FPrimitiveSceneProxy* CreateSceneProxy ( ) override;

public:

	virtual void NotifyMeshUpdated ( ) override;

private:

	std::atomic < bool > bUpdatingThreadData = false;

	FCriticalSection ThreadDataLock;

	TArray < FLumenCardBuildData > CurrentLumenCardData = TArray < FLumenCardBuildData > ( );
	FBox                           CurrentLumenBound    = FBox ( );

	TAsyncMarchingData MeshComputeData = TAsyncMarchingData ( this );

	static void ComputeNewMarchingMesh_TaskFunction ( TUniquePtr < FLFPMarchingThreadData >& ThreadData , FProgressCancel& Progress , const TBitArray < >& SolidList , const FLFPMarchingPassData& PassData );

	void ComputeNewMarchingMesh_Completed ( TUniquePtr < FLFPMarchingThreadData >& ThreadData , TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc >& GameThreadJob );

private:

	std::atomic < bool > bUpdatingDistanceFieldData = false;

	FTimerHandle DistanceFieldBatchHandler;

	FCriticalSection DistanceFieldDataLock;

	TSharedPtr < FDistanceFieldVolumeData , ESPMode::ThreadSafe > DistanceFieldData = nullptr;

	TAsyncMarchingData DistanceFieldComputeData = TAsyncMarchingData ( this );

	// Modify to use ParallelFor. Copy from dynamic mesh
	static TUniquePtr < FDistanceFieldVolumeData > ComputeNewDistanceField_TaskFunctionV2 ( FProgressCancel& Progress , const FDynamicMesh3& Mesh , const bool bGenerateAsIfTwoSided , const float CurrentDistanceFieldResolutionScale );

	// Add Safety
	void ComputeNewDistanceFieldData_Completed ( TUniquePtr < FDistanceFieldVolumeData > NewData , TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc >& GameThreadJob );
};
