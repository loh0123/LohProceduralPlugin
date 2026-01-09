// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "Components/LPPMarchingMeshComponent.h"

#include "DynamicMeshEditor.h"
#include "MeshAdapterTransforms.h"
#include "MeshCardBuild.h"
#include "MeshSimplification.h"
#include "Components/LFPChunkedGridPositionComponent.h"
#include "Components/LFPChunkedTagDataComponent.h"
#include "Data/LPPDynamicMeshRenderData.h"
#include "Data/LPPMarchingData.h"
#include "DynamicMesh/MeshNormals.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"
#include "Library/LPPDynamicMeshLibrary.h"
#include "Library/LPPMarchingFunctionLibrary.h"
#include "Math/LFPGridLibrary.h"
#include "Operations/MeshPlaneCut.h"
#include "Parameterization/DynamicMeshUVEditor.h"
#include "Polygroups/PolygroupsGenerator.h"
#include "Render/LFPRenderLibrary.h"
#include "Runtime/GeometryFramework/Private/Components/DynamicMeshSceneProxy.h"
#include "Windows/WindowsSemaphore.h"


LLM_DEFINE_TAG ( LFPMarchingMesh );

// Sets default values for this component's properties
ULPPMarchingMeshComponent::ULPPMarchingMeshComponent ( )
{
	// Set this component to be initialized when the game starts and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}

// Called when the game starts
void ULPPMarchingMeshComponent::BeginPlay ( )
{
	Super::BeginPlay ( );

	// ...
}

void ULPPMarchingMeshComponent::EndPlay ( const EEndPlayReason::Type EndPlayReason )
{
	Super::EndPlay ( EndPlayReason );
}

// Called every frame
void ULPPMarchingMeshComponent::TickComponent ( float DeltaTime , ELevelTick TickType , FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent ( DeltaTime , TickType , ThisTickFunction );

	// ...
}

FIntVector ULPPMarchingMeshComponent::GetDataSize ( ) const
{
	if ( IsDataComponentValid ( ) )
	{
		return PositionComponent->GetDataGridSize ( );
	}

	return FIntVector::NoneValue;
}

int32 ULPPMarchingMeshComponent::GetDataNum ( ) const
{
	const FIntVector& DataSize = GetDataSize ( );

	if ( DataSize == FIntVector::NoneValue )
	{
		return INDEX_NONE;
	}

	return DataSize.X * DataSize.Y * DataSize.Z;
}

FVector ULPPMarchingMeshComponent::GetMeshSize ( ) const
{
	if ( IsDataComponentValid ( ) )
	{
		return RenderSetting->GetMarchingSize ( );
	}

	return FVector ( 0.0f );
}

bool ULPPMarchingMeshComponent::IsDataComponentValid ( ) const
{
	return IsValid ( GetWorld ( ) ) && IsValid ( DataComponent ) && IsValid ( PositionComponent ) && IsValid ( RenderSetting ) && DataComponent->IsChunkIndexValid ( RegionIndex , ChunkIndex );
}

void ULPPMarchingMeshComponent::GetFaceCullingSetting ( bool& bIsChunkFaceCullingDisable , bool& bIsRegionFaceCullingDisable ) const
{
	bIsChunkFaceCullingDisable  = false;
	bIsRegionFaceCullingDisable = false;

	if ( IsDataComponentValid ( ) )
	{
		bIsChunkFaceCullingDisable  = RenderSetting->IsChunkFaceCullingDisable ( );
		bIsRegionFaceCullingDisable = RenderSetting->IsRegionFaceCullingDisable ( );
	}
}

uint8 ULPPMarchingMeshComponent::GetMarchingID ( const FIntVector& Offset ) const
{
	if ( IsDataComponentValid ( ) )
	{
		uint8 MarchingID = 0;

		for ( int32 MarchingIndex = 0 ; MarchingIndex < 8 ; ++MarchingIndex )
		{
			const FIntVector   MarchingOffset = ULFPGridLibrary::ToGridLocation ( MarchingIndex , FIntVector ( 2 ) );
			const FIntVector   DataIndex      = PositionComponent->AddOffsetToDataGridIndex ( FIntVector ( RegionIndex , ChunkIndex , 0 ) , Offset + MarchingOffset );
			const FGameplayTag DataTag        = DataComponent->GetDataTag ( DataIndex.X , DataIndex.Y , DataIndex.Z );

			if ( DataTag.MatchesTag ( HandleTag ) )
			{
				MarchingID |= ( 1 << MarchingIndex );
			}
		}

		return MarchingID;
	}

	return 0;
}

void ULPPMarchingMeshComponent::Initialize ( ULFPChunkedTagDataComponent* NewDataComponent , ULFPChunkedGridPositionComponent* NewPositionComponent , const int32 NewRegionIndex , const int32 NewChunkIndex )
{
	if ( IsDataComponentValid ( ) )
	{
		Uninitialize ( );
	}

	DataComponent     = NewDataComponent;
	PositionComponent = NewPositionComponent;
	RegionIndex       = NewRegionIndex;
	ChunkIndex        = NewChunkIndex;
}

void ULPPMarchingMeshComponent::Uninitialize ( )
{
	DataComponent = nullptr;

	ClearRender ( );
}

void ULPPMarchingMeshComponent::ClearRender ( )
{
	MeshComputeData.CancelJob ( );
	DistanceFieldComputeData.CancelJob ( );

	{
		FScopeLock TDLock ( &RenderDataLock );

		MeshRenderData.Reset ( );
	}

	AggGeom.EmptyElements ( );

	ClearMesh ( );
}

bool ULPPMarchingMeshComponent::UpdateRender ( )
{
	if ( IsDataComponentValid ( ) == false )
	{
		ClearRender ( );

		return false;
	}

	const FIntVector& CacheDataSize  = GetDataSize ( ) + FIntVector ( 2 );
	const int32       CacheDataIndex = CacheDataSize.X * CacheDataSize.Y * CacheDataSize.Z;

	int32 ValidCount = 0;

	if ( DataComponent->GetDataTagList ( RegionIndex , ChunkIndex ).IsEmpty ( ) )
	{
		ClearRender ( );

		OnMeshSkipOnEmpty.Broadcast ( this );

		return false;
	}

	TBitArray < > CacheDataList = TBitArray ( false , CacheDataIndex );
	{
		/* Generate Marching Mesh Data */
		for ( int32 SolidIndex = 0 ; SolidIndex < CacheDataIndex ; ++SolidIndex )
		{
			const FIntVector CheckOffset = ULFPGridLibrary::ToGridLocation ( SolidIndex , CacheDataSize ) - FIntVector ( 1 );
			const FIntVector CheckIndex  = PositionComponent->AddOffsetToDataGridIndex ( FIntVector ( RegionIndex , ChunkIndex , 0 ) , CheckOffset );

			if ( CheckIndex.GetMin ( ) == INDEX_NONE )
			{
				continue;
			}

			CacheDataList [ SolidIndex ] = DataComponent->GetDataTag ( CheckIndex.X , CheckIndex.Y , CheckIndex.Z ).MatchesTag ( HandleTag );

			if ( CacheDataList [ SolidIndex ] )
			{
				ValidCount += 1;
			}
		}
	}

	// Complete Empty No Need Render Or Inside Other Chunk So Don't Need To Render
	if ( ValidCount == 0 || CacheDataIndex == ValidCount )
	{
		ClearRender ( );

		if ( CacheDataIndex == ValidCount )
		{
			OnMeshSkipOnFull.Broadcast ( this );
		}
		else
		{
			OnMeshSkipOnEmpty.Broadcast ( this );
		}

		return false;
	}

	OnMeshRebuilding.Broadcast ( this );

	FLFPMarchingPassData PassData;

	GetFaceCullingSetting ( PassData.bIsChunkFaceCullingDisable , PassData.bIsRegionFaceCullingDisable );

	PassData.bRenderData       = bGenerateRenderData;
	PassData.MeshFullSize      = GetMeshSize ( );
	PassData.DataSize          = GetDataSize ( );
	PassData.BoundExpand       = BoundExpand;
	PassData.EdgeMergeDistance = EdgeMergeDistance;

	PassData.bSimplifyRenderData = bSimplifyRenderData;
	PassData.SimplifyAngle       = SimplifyAngle;

	PassData.bSimpleBoxCollisionData = bGenerateSimpleBoxCollisionData;

	PassData.bRecomputeBoxUV = bRecomputeBoxUV;
	PassData.UVBoxTransform  = UVBoxTransform;

	PassData.StartTime = FDateTime::UtcNow ( );

	PassData.RenderSetting = RenderSetting;

	PassData.DataID = ClearMeshCounter;

	// Compute whether the mesh uses mainly two-sided materials before, as this is the only info the distance field compute needs from the mesh attributes
	bool bMostlyTwoSided = bTwoSideDistanceField || IsValid ( GetMaterial ( 0 ) ) ? GetMaterial ( 0 )->IsTwoSided ( ) : false;

	PassData.bMostlyTwoSided = bMostlyTwoSided;
	PassData.bNaniteMesh     = bGenerateNaniteMesh;

	MeshComputeData.LaunchJob ( TEXT ( "MarchingDynamicMeshComponentMeshData" ) ,
	                            [this, MovedCacheDataList = MoveTemp ( CacheDataList ),MovedPassData = MoveTemp ( PassData )] ( FProgressCancel& Progress , TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc >& GameThreadJob )
	                            {
		                            LLM_SCOPE_BYTAG ( LFPMarchingMesh );

		                            TUniquePtr < FLFPMarchingThreadData > ThreadData = MakeUnique < FLFPMarchingThreadData > ( );;

		                            ComputeNewMarchingMesh_TaskFunction ( ThreadData , Progress , MovedCacheDataList , MovedPassData );

		                            if ( Progress.Cancelled ( ) == false )
		                            {
			                            ComputeNewMarchingMesh_Completed ( ThreadData , GameThreadJob );
		                            }

		                            if ( ThreadData.IsValid ( ) )
		                            {
			                            ThreadData.Reset ( );
		                            }

		                            check ( ThreadData.Get ( ) == nullptr );
	                            } );

	return true;
}

void ULPPMarchingMeshComponent::UpdateDistanceField ( )
{
	if ( bGenerateDistanceField == false )
	{
		return;
	}

	{
		FScopeLock Lock ( &RenderDataLock );

		if ( DistanceFieldResolutionScale <= 0.0f || MeshRenderData.IsValid ( ) == false || MeshRenderData->MeshData.TriangleCount ( ) == 0 || IsDataComponentValid ( ) == false )
		{
			DistanceFieldComputeData.CancelJob ( );

			if ( MeshRenderData.IsValid ( ) )
			{
				MeshRenderData->DistanceFieldPtr.Reset ( );
			}

			return;
		}
	}

	// Fallback Mesh
	if ( MeshRenderData.IsValid ( ) &&
	     MeshRenderData->DistanceFieldPtr.IsValid ( ) == false &&
	     IsValid ( DistanceFieldFallBackMesh ) &&
	     DistanceFieldFallBackMesh->IsCompiling ( ) == false &&
	     DistanceFieldFallBackMesh->GetRenderData ( ) != nullptr &&
	     DistanceFieldFallBackMesh->GetRenderData ( )->GetCurrentFirstLOD ( 0 )->DistanceFieldData != nullptr
	)
	{
		MeshRenderData->DistanceFieldPtr = MakeShared < FDistanceFieldVolumeData > ( );

		*MeshRenderData->DistanceFieldPtr = *DistanceFieldFallBackMesh->GetRenderData ( )->GetCurrentFirstLOD ( 0 )->DistanceFieldData; // Copy Data
	}

	GetWorld ( )->GetTimerManager ( ).ClearTimer ( DistanceFieldBatchHandler );

	// For safety, run the distance field compute on a (geometry-only) copy of the mesh
	FDynamicMesh3 GeoOnlyCopy;

	// Compute whether the mesh uses mainly two-sided materials before, as this is the only info the distance field compute needs from the mesh attributes
	bool bMostlyTwoSided = bTwoSideDistanceField || IsValid ( GetMaterial ( 0 ) ) ? GetMaterial ( 0 )->IsTwoSided ( ) : false;

	GeoOnlyCopy.Copy ( MeshRenderData->MeshData , false , false , false , false );

	float CurrentDistanceFieldBatchTime = DistanceFieldBatchTime;
	{
		if ( IsValid ( GetWorld ( ) ) && IsValid ( GetWorld ( )->GetFirstPlayerController ( ) ) )
		{
			const auto  PlayerLocation = GetWorld ( )->GetFirstPlayerController ( )->K2_GetActorLocation ( );
			const float PriorityTime   = FMath::Max ( FVector::Distance ( PlayerLocation , GetComponentLocation ( ) ) / DistanceFieldPriorityDistance , 1.0f );

			CurrentDistanceFieldBatchTime *= PriorityTime;
		}
	}

	GetWorld ( )->GetTimerManager ( ).SetTimer ( DistanceFieldBatchHandler , [this , GeoOnlyCopy , bMostlyTwoSided] ( ) mutable
	{
		if ( IsValid ( this ) == false )
		{
			return;
		}

		if ( bGenerateDistanceField == false )
		{
			return;
		}

		{
			FScopeLock Lock ( &RenderDataLock );

			if ( DistanceFieldResolutionScale <= 0.0f || MeshRenderData.IsValid ( ) == false || MeshRenderData->MeshData.TriangleCount ( ) == 0 || IsDataComponentValid ( ) == false )
			{
				DistanceFieldComputeData.CancelJob ( );

				if ( MeshRenderData.IsValid ( ) )
				{
					MeshRenderData->DistanceFieldPtr.Reset ( );
				}

				return;
			}
		}

		OnDistanceFieldRebuilding.Broadcast ( this );

		// Fill Mesh Hole
		{
			const FIntVector& DataSize = GetDataSize ( );

			const int32 DataNum = GetDataNum ( );

			const FVector MeshFullSize = GetMeshSize ( );
			const FVector MeshHalfSize = MeshFullSize * 0.5f;

			const FVector MeshBoundFullSize = MeshFullSize * FVector ( DataSize );
			const FVector MeshBoundHalfSize = MeshBoundFullSize * 0.5f;

			const auto& CreateFace = [&] ( const int32& VoxelIndex , const int32& RotationID )
			{
				const FIntVector         VoxelGridPos                  = ULFPGridLibrary::ToGridLocation ( VoxelIndex , DataSize );
				const FVector            CenterPos                     = ( FVector ( VoxelGridPos ) + 0.5f ) * MeshFullSize;
				const FRotator           Rotation                      = LFPMarchingRenderConstantData::VertexRotationList [ RotationID ];
				const TArray < FVector > FaceVertexList                = ULFPRenderLibrary::CreateVertexPosList ( CenterPos , Rotation , MeshHalfSize );
				const FVector            FaceVertexPosList [ 2 ] [ 3 ] =
				{
					{
						FaceVertexList [ 1 ] , FaceVertexList [ 0 ] , FaceVertexList [ 3 ]
					} ,
					{
						FaceVertexList [ 2 ] , FaceVertexList [ 3 ] , FaceVertexList [ 0 ]
					}
				};

				for ( int32 FaceIndex = 0 ; FaceIndex < 2 ; ++FaceIndex )
				{
					FIntVector FaceVertexIndexList;

					for ( int32 FaceVertexIndex = 0 ; FaceVertexIndex < 3 ; ++FaceVertexIndex )
					{
						const FVector& FaceVertexPos = FaceVertexPosList [ FaceIndex ] [ FaceVertexIndex ] - MeshBoundHalfSize;

						FaceVertexIndexList [ FaceVertexIndex ] = GeoOnlyCopy.AppendVertex ( FaceVertexPos );
					}

					GeoOnlyCopy.AppendTriangle ( FaceVertexIndexList );
				}
			};

			/* Generate Voxel Mesh Data */
			for ( int32 DataIndex = 0 ; DataIndex < DataNum ; ++DataIndex )
			{
				const FIntVector VoxelPos       = ULFPGridLibrary::ToGridLocation ( DataIndex , DataSize );
				const FIntVector VoxelDataIndex = PositionComponent->AddOffsetToDataGridIndex ( FIntVector ( RegionIndex , ChunkIndex , 0 ) , VoxelPos );

				const FGameplayTag& SelfVoxelTag = DataComponent->GetDataTag ( VoxelDataIndex.X , VoxelDataIndex.Y , VoxelDataIndex.Z );

				if ( SelfVoxelTag.MatchesTag ( HandleTag ) )
				{
					for ( int32 FaceDirectionIndex = 0 ; FaceDirectionIndex < 6 ; ++FaceDirectionIndex )
					{
						const FIntVector& TargetIndex = PositionComponent->AddOffsetToDataGridIndex ( VoxelDataIndex , LFPMarchingRenderConstantData::FaceDirection [ FaceDirectionIndex ].Up );

						const bool bForceRender = ULFPGridLibrary::IsGridLocationValid ( VoxelPos + LFPMarchingRenderConstantData::FaceDirection [ FaceDirectionIndex ].Up , DataSize ) == false;

						const bool TargetVoxelValid = TargetIndex.GetMin ( ) != INDEX_NONE && DataComponent->GetDataTag ( TargetIndex.X , TargetIndex.Y , TargetIndex.Z ).MatchesTag ( HandleTag );

						// Check Is Border
						if ( bForceRender && TargetVoxelValid )
						{
							CreateFace ( DataIndex , FaceDirectionIndex );
						}
					}
				}
			}
		}

		const float CurrentDistanceFieldResolutionScale = DistanceFieldResolutionScale;

		DistanceFieldComputeData.LaunchJob ( TEXT ( "MarchingDynamicMeshComponentDistanceField" ) ,
		                                     [this,GeoOnlyCopy , CurrentDistanceFieldResolutionScale, bMostlyTwoSided] ( FProgressCancel& Progress , TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc >& GameThreadJob )
		                                     {
			                                     TUniquePtr < FDistanceFieldVolumeData > ThreadData = MakeUnique < FDistanceFieldVolumeData > ( );

			                                     ULPPDynamicMeshLibrary::BuildDynamicMeshDistanceField ( *ThreadData.Get ( ) , Progress , GeoOnlyCopy , bMostlyTwoSided , CurrentDistanceFieldResolutionScale );

			                                     if ( Progress.Cancelled ( ) == false )
			                                     {
				                                     ComputeNewDistanceFieldData_Completed ( ThreadData , GameThreadJob );
			                                     }

			                                     if ( ThreadData.IsValid ( ) )
			                                     {
				                                     ThreadData.Reset ( );
			                                     }

			                                     check ( ThreadData.Get ( ) == nullptr );
		                                     } );
	} , CurrentDistanceFieldBatchTime , false );
}

void ULPPMarchingMeshComponent::NotifyMeshUpdated ( )
{
	Super::NotifyMeshUpdated ( );

	UpdateDistanceField ( );
}

void ULPPMarchingMeshComponent::ComputeNewMarchingMesh_TaskFunction ( TUniquePtr < FLFPMarchingThreadData >& ThreadData , FProgressCancel& Progress , const TBitArray < >& SolidList , const FLFPMarchingPassData& PassData )
{
	if ( Progress.Cancelled ( ) )
	{
		return;
	}

	TRACE_CPUPROFILER_EVENT_SCOPE ( MarchingMesh_GeneratingThreadData );

	FDateTime WorkTime    = FDateTime::UtcNow ( );
	ThreadData->StartTime = PassData.StartTime;
	ThreadData->DataID    = PassData.DataID;

	const FVector& MeshFullSize = PassData.MeshFullSize;
	const FVector  MeshGapSize  = MeshFullSize;

	const FIntVector& DataSize      = PassData.DataSize;
	const FIntVector& CacheDataSize = DataSize + FIntVector ( 2 );

	const FVector MeshBoundFullSize = MeshGapSize * FVector ( DataSize );
	const FVector MeshBoundHalfSize = MeshBoundFullSize * FVector ( 0.5f );

	const FIntVector MarchingSize = DataSize + FIntVector ( 1 );
	const int32      MarchingNum  = MarchingSize.X * MarchingSize.Y * MarchingSize.Z;

	const FBox CurrentLocalBounds = FBox ( -MeshBoundHalfSize , MeshBoundHalfSize );

	const float BoundExpand = PassData.BoundExpand;

	const auto GetMarchingID = [&] ( const FIntVector& StartPosition )
	{
		uint8 MarchingID = 0;

		for ( int32 MarchingIndex = 0 ; MarchingIndex < 8 ; ++MarchingIndex )
		{
			const FIntVector MarchingPosition = ULFPGridLibrary::ToGridLocation ( MarchingIndex , FIntVector ( 2 ) ) + StartPosition;
			const int32      SolidIndex       = ULFPGridLibrary::ToGridIndex ( MarchingPosition , DataSize + FIntVector ( 2 ) );

			if ( SolidList [ SolidIndex ] )
			{
				MarchingID |= 1 << MarchingIndex;
			}
		}

		return MarchingID;
	};

	if ( Progress.Cancelled ( ) )
	{
		return;
	}

	// Mesh Data
	if ( PassData.bRenderData )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE ( MarchingMesh_GeneratingMesh );

		FDynamicMesh3* MeshData = &ThreadData->MeshData;
		{
			MeshData->Clear ( );
			MeshData->EnableAttributes ( );
			MeshData->Attributes ( )->EnableMaterialID ( );
		}

		bool bHasMesh = false;

		TArray < uint8 > MarchingIDList;
		{
			MarchingIDList.SetNum ( MarchingNum );

			for ( int32 MarchingIndex = 0 ; MarchingIndex < MarchingNum ; ++MarchingIndex )
			{
				const FIntVector CheckPos = ULFPGridLibrary::ToGridLocation ( MarchingIndex , MarchingSize );

				MarchingIDList [ MarchingIndex ] = GetMarchingID ( CheckPos );

				bHasMesh |= MarchingIDList [ MarchingIndex ] != 0 && MarchingIDList [ MarchingIndex ] != 255;
			}
		}

		if ( bHasMesh )
		{
			const TObjectPtr < ULPPMarchingData >& MeshAsset = PassData.RenderSetting;

			UE::Geometry::FDynamicMeshEditor Editor ( MeshData );

			/* Generate Marching Mesh Data */
			for ( int32 MarchingIndex = 0 ; MarchingIndex < MarchingNum ; ++MarchingIndex )
			{
				const FIntVector MarchingGridLocation = ULFPGridLibrary::ToGridLocation ( MarchingIndex , MarchingSize );
				const FVector    MarchingMeshLocation = ( MeshGapSize * FVector ( MarchingGridLocation ) ) - MeshBoundHalfSize;
				const uint8      MarchingID           = MarchingIDList [ MarchingIndex ];

				if ( Progress.Cancelled ( ) )
				{
					return;
				}

				if ( const FLFPMarchingMeshMappingDataV2& MeshMappingData = MeshAsset->GetMappingData ( MarchingID ) ; MeshMappingData.MeshID != INDEX_NONE )
				{
					const FDynamicMesh3* AppendMesh = MeshAsset->GetDynamicMesh ( MeshMappingData.MeshID );

					if ( AppendMesh == nullptr )
					{
						UE_LOG ( LogTemp , Error , TEXT ( "Marching Data : Mesh Invalid %i" ) , MeshMappingData.MeshID );

						continue;
					}

					const FTransform AppendTransform (
					                                  MeshMappingData.GetRotation ( ) ,
					                                  MarchingMeshLocation ,
					                                  FVector ( 1.0f )
					                                 );

					UE::Geometry::FTransformSRT3d    XForm ( AppendTransform );
					UE::Geometry::FMeshIndexMappings TmpMappings;
					Editor.AppendMesh ( AppendMesh , TmpMappings ,
					                    [&XForm] ( const int32 VID , const FVector3d& Position ) { return XForm.TransformPosition ( Position ); } ,
					                    [&XForm] ( const int32 NID , const FVector3d& Normal ) { return XForm.TransformNormal ( Normal ); } ,
					                    XForm.GetDeterminant ( ) < 0 );
				}
			}

			{
				UE::Geometry::FMergeCoincidentMeshEdges Welder ( MeshData );
				Welder.MergeVertexTolerance = PassData.EdgeMergeDistance;
				Welder.OnlyUniquePairs      = false;
				Welder.Apply ( );
			}

			{
				const FVector EdgeDirectionList [ 6 ] =
				{
					FVector ( 1 , 0 , 0 ) ,
					FVector ( 0 , 1 , 0 ) ,
					FVector ( 0 , 0 , 1 ) ,
					FVector ( -1 , 0 , 0 ) ,
					FVector ( 0 , -1 , 0 ) ,
					FVector ( 0 , 0 , -1 )
				};

				for ( const FVector& EdgeDirection : EdgeDirectionList )
				{
					UE::Geometry::FMeshPlaneCut Cut ( MeshData , EdgeDirection * MeshBoundHalfSize , EdgeDirection );
					Cut.bCollapseDegenerateEdgesOnCut = false;
					Cut.Cut ( );
				}
			}

			if ( PassData.bRecomputeBoxUV )
			{
				FDynamicMeshUVOverlay*             UVOverlay = MeshData->Attributes ( )->PrimaryUV ( );
				UE::Geometry::FDynamicMeshUVEditor UVEditor ( MeshData , UVOverlay );

				TArray < int32 > TriangleROI;
				for ( const int32 TriangleID : MeshData->TriangleIndicesItr ( ) )
				{
					TriangleROI.Add ( TriangleID );
				}

				constexpr int32 MinIslandTriCount = 2;

				MeshAdapterTransforms::FFrame3d ProjectionFrame ( PassData.UVBoxTransform );
				UVEditor.SetTriangleUVsFromBoxProjection ( TriangleROI , [&] ( const FVector3d& Pos ) { return Pos; } ,
				                                           ProjectionFrame , PassData.UVBoxTransform.GetScale3D ( ) , MinIslandTriCount );
			}

			if ( PassData.bSimplifyRenderData )
			{
				UE::Geometry::FQEMSimplification Simplifier ( MeshData );

				Simplifier.bAllowSeamCollapse     = true;
				Simplifier.bPreserveBoundaryShape = true;
				Simplifier.SimplifyToMinimalPlanar ( PassData.SimplifyAngle );

				UE::Geometry::FMeshNormals MeshNormals ( MeshData );
				MeshNormals.RecomputeOverlayNormals ( MeshData->Attributes ( )->PrimaryNormals ( ) , true , true );
				MeshNormals.CopyToOverlay ( MeshData->Attributes ( )->PrimaryNormals ( ) , false );
			}

			MeshData->RemoveUnusedVertices ( );
			MeshData->CompactInPlace ( nullptr );

			{
				UE::Geometry::FPolygroupsGenerator Generator ( MeshData );
				Generator.bApplyPostProcessing = false;
				Generator.bCopyToMesh          = true;
				Generator.FindPolygroupsFromConnectedTris ( );
			}
		}
	}

	if ( Progress.Cancelled ( ) )
	{
		return;
	}

	// Lumen Card
	if ( PassData.bRenderData )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE ( MarchingMesh_GeneratingLumen );

		auto SetCoverIndex = [&] ( FIntPoint& CoverIndex , const int32 Index )
		{
			if ( Index <= -1 )
			{
				CoverIndex = FIntPoint ( INDEX_NONE );
			}
			else if ( CoverIndex.GetMin ( ) == INDEX_NONE )
			{
				CoverIndex = FIntPoint ( Index );
			}
			else if ( CoverIndex.X > Index )
			{
				CoverIndex.X = Index;
			}
			else if ( CoverIndex.Y < Index )
			{
				CoverIndex.Y = Index;
			}
		};

		auto CheckFaceVisible = [&] ( const FIntVector& CurrentPos , const FIntVector& FromDirection )
		{
			const FIntVector TargetCheckPos  = CurrentPos + FIntVector ( 1 );
			const FIntVector CurrentCheckPos = TargetCheckPos + FromDirection;

			const int32 TargetIndex  = ULFPGridLibrary::ToGridIndex ( TargetCheckPos , CacheDataSize );
			const int32 CurrentIndex = ULFPGridLibrary::ToGridIndex ( CurrentCheckPos , CacheDataSize );

			return SolidList [ CurrentIndex ] == false && SolidList [ TargetIndex ];
		};

		auto AddCardBuild = [&] ( TArray < FLumenCardBuildData >& CardBuildList , const FIntPoint& CoverIndex , const int32 DirectionIndex )
		{
			FBox3f LumenBox;

			switch ( DirectionIndex )
			{
				case 0 :
				case 3 : LumenBox = FBox3f (
				                            FVector3f ( CurrentLocalBounds.Min.X , CurrentLocalBounds.Min.Y , ( CoverIndex.X * MeshFullSize.Z ) - MeshBoundHalfSize.Z - BoundExpand ) ,
				                            FVector3f ( CurrentLocalBounds.Max.X , CurrentLocalBounds.Max.Y , ( CoverIndex.Y * MeshFullSize.Z ) - MeshBoundHalfSize.Z + BoundExpand )
				                           );

					if ( DirectionIndex == 0 )
					{
						LumenBox = LumenBox.ShiftBy ( FVector3f ( 0.0f , 0.0f , MeshFullSize.Z ) );
					}
					break;

				case 1 :
				case 4 : LumenBox = FBox3f (
				                            FVector3f ( ( CoverIndex.X * MeshFullSize.X ) - MeshBoundHalfSize.X - BoundExpand , CurrentLocalBounds.Min.Y , CurrentLocalBounds.Min.Z ) ,
				                            FVector3f ( ( CoverIndex.Y * MeshFullSize.X ) - MeshBoundHalfSize.X + BoundExpand , CurrentLocalBounds.Max.Y , CurrentLocalBounds.Max.Z )
				                           );

					if ( DirectionIndex == 4 )
					{
						LumenBox = LumenBox.ShiftBy ( FVector3f ( MeshFullSize.X , 0.0f , 0.0f ) );
					}
					break;

				case 2 :
				case 5 : LumenBox = FBox3f (
				                            FVector3f ( CurrentLocalBounds.Min.X , ( CoverIndex.X * MeshFullSize.Y ) - MeshBoundHalfSize.Y - BoundExpand , CurrentLocalBounds.Min.Z ) ,
				                            FVector3f ( CurrentLocalBounds.Max.X , ( CoverIndex.Y * MeshFullSize.Y ) - MeshBoundHalfSize.Y + BoundExpand , CurrentLocalBounds.Max.Z )
				                           );

					if ( DirectionIndex == 2 )
					{
						LumenBox = LumenBox.ShiftBy ( FVector3f ( 0.0f , MeshFullSize.Y , 0.0f ) );
					}
					break;
				default : break;
			}

			FLumenCardBuildData BuildData;

			BuildData.AxisAlignedDirectionIndex = LFPMarchingRenderConstantData::SurfaceDirectionID [ DirectionIndex ];

			LFPMarchingRenderConstantData::FaceDirection [ DirectionIndex ].SetAxis (
			                                                                         BuildData.OBB.AxisX ,
			                                                                         BuildData.OBB.AxisY ,
			                                                                         BuildData.OBB.AxisZ
			                                                                        );

			BuildData.OBB.Origin = FVector3f ( LumenBox.GetCenter ( ) );
			BuildData.OBB.Extent = FVector3f ( LFPMarchingRenderConstantData::VertexRotationList [ DirectionIndex ].UnrotateVector ( FVector ( LumenBox.GetExtent ( ) ) ).GetAbs ( ) );

			CardBuildList.Add ( BuildData );
		};

		ThreadData->LumenCardData.bMostlyTwoSided = PassData.bMostlyTwoSided;

		ThreadData->LumenCardData.Bounds = FBox ( CurrentLocalBounds );

		TArray < FLumenCardBuildData >& CardBuildList = ThreadData->LumenCardData.CardBuildData;

		CardBuildList.Empty ( );

		struct FLFPFaceListData
		{
			FLFPFaceListData ( const FIntVector InID , const int32 InFaceID , const bool InbIsReverse ) : ID ( InID ), FaceID ( InFaceID ), bIsReverse ( InbIsReverse )
			{
			}

			const FIntVector ID         = FIntVector ( 0 , 1 , 2 );
			const int32      FaceID     = 0;
			const bool       bIsReverse = false;
		};

		constexpr bool FaceReverseList [ ] =
		{
			true , false , true , false , true , false
		};

		for ( int32 Direction = 0 ; Direction < 6 ; ++Direction )
		{
			if ( Progress.Cancelled ( ) )
			{
				return;
			}

			const FIntVector MarchingDimension = FIntVector (
			                                                 DataSize [ LFPMarchingRenderConstantData::FaceLoopDirectionList [ Direction ].X ] ,
			                                                 DataSize [ LFPMarchingRenderConstantData::FaceLoopDirectionList [ Direction ].Y ] ,
			                                                 DataSize [ LFPMarchingRenderConstantData::FaceLoopDirectionList [ Direction ].Z ]
			                                                );

			const int32 MarchingPlaneLength = MarchingDimension.X * MarchingDimension.Y;

			FIntPoint CoverIndex = FIntPoint ( INDEX_NONE );

			TBitArray < > BlockMap = TBitArray ( false , MarchingPlaneLength );

			const bool& bIsReverse = FaceReverseList [ Direction ];

			for ( int32 DepthIndex = bIsReverse
			                         ? MarchingDimension.Z - 1
			                         : 0 ; bIsReverse
			                               ? DepthIndex > -1
			                               : DepthIndex < MarchingDimension.Z ; bIsReverse
			                                                                    ? DepthIndex--
			                                                                    : DepthIndex++ )
			{
			StartCheck:

				for ( int32 X = 0 ; X < MarchingDimension.X ; X++ )
				{
					for ( int32 Y = 0 ; Y < MarchingDimension.Y ; Y++ )
					{
						FIntVector MarchingGridLocation;
						MarchingGridLocation [ LFPMarchingRenderConstantData::FaceLoopDirectionList [ Direction ].X ] = X;
						MarchingGridLocation [ LFPMarchingRenderConstantData::FaceLoopDirectionList [ Direction ].Y ] = Y;
						MarchingGridLocation [ LFPMarchingRenderConstantData::FaceLoopDirectionList [ Direction ].Z ] = DepthIndex;

						const int32 MarchingPlaneIndex = X + ( Y * MarchingDimension.X );

						const bool bIsFaceVisible = CheckFaceVisible ( MarchingGridLocation , LFPMarchingRenderConstantData::FaceDirection [ Direction ].Up );

						if ( BlockMap [ MarchingPlaneIndex ] )
						{
							if ( bIsFaceVisible )
							{
								AddCardBuild ( CardBuildList , CoverIndex , Direction );

								/* Reset */
								BlockMap = TBitArray ( false , MarchingPlaneLength );

								SetCoverIndex ( CoverIndex , INDEX_NONE );

								goto StartCheck;
							}
						}
						else if ( bIsFaceVisible )
						{
							BlockMap [ MarchingPlaneIndex ] = bIsFaceVisible;

							SetCoverIndex ( CoverIndex , DepthIndex );
						}
					}
				}
			}

			if ( CoverIndex.GetMin ( ) != INDEX_NONE )
			{
				AddCardBuild ( CardBuildList , CoverIndex , Direction );
			}
		}
	}

	if ( Progress.Cancelled ( ) )
	{
		return;
	}

	// Collision Box
	if ( PassData.bSimpleBoxCollisionData )
	{
		TRACE_CPUPROFILER_EVENT_SCOPE ( MarchingMesh_GeneratingBoxCollision );

		TMap < FIntVector , FIntVector > BatchDataMap;

		TPair < FIntVector , FIntVector > CurrentBatchData ( INDEX_NONE , INDEX_NONE );

		bool IsBatchValid = false;

		const auto& PushData = [&] ( )
		{
			{
				const FIntVector  TargetMax    = CurrentBatchData.Key - FIntVector ( 0 , 1 , 0 );
				const FIntVector* TargetMinPtr = BatchDataMap.Find ( TargetMax );

				if ( TargetMinPtr != nullptr && TargetMinPtr->X == CurrentBatchData.Value.X && TargetMinPtr->Z == CurrentBatchData.Value.Z )
				{
					const FIntVector TargetMin = BatchDataMap.FindAndRemoveChecked ( TargetMax );

					CurrentBatchData.Value.Y = TargetMin.Y;
				}
			}

			{
				const FIntVector  TargetMax    = CurrentBatchData.Key - FIntVector ( 0 , 0 , 1 );
				const FIntVector* TargetMinPtr = BatchDataMap.Find ( TargetMax );

				if ( TargetMinPtr != nullptr && TargetMinPtr->X == CurrentBatchData.Value.X && TargetMinPtr->Y == CurrentBatchData.Value.Y )
				{
					const FIntVector TargetMin = BatchDataMap.FindAndRemoveChecked ( TargetMax );

					CurrentBatchData.Value.Z = TargetMin.Z;
				}
			}

			BatchDataMap.Add ( CurrentBatchData.Key , CurrentBatchData.Value );
			IsBatchValid = false;
		};

		/** Generate Batch Data Map */
		for ( int32 Z = 0 ; Z < DataSize.Z ; Z++ )
		{
			if ( Progress.Cancelled ( ) )
			{
				return;
			}

			for ( int32 Y = 0 ; Y < DataSize.Y ; Y++ )
			{
				for ( int32 X = 0 ; X < DataSize.X ; X++ )
				{
					/***************** Identify Data *****************/
					const FIntVector CurrentPos ( X , Y , Z );

					const int32 CurrentIndex = ULFPGridLibrary::ToGridIndex ( CurrentPos + FIntVector ( 1 ) , CacheDataSize );

					/*************************************************/

					if ( SolidList [ CurrentIndex ] )
					{
						if ( ( CurrentBatchData.Key.Y != Y || CurrentBatchData.Key.Z != Z ) && IsBatchValid )
						{
							PushData ( );
						}

						if ( IsBatchValid )
						{
							CurrentBatchData.Key.X += 1;
						}
						else
						{
							CurrentBatchData.Key   = CurrentPos;
							CurrentBatchData.Value = CurrentPos;
							IsBatchValid           = true;
						}
					}
					else if ( IsBatchValid )
					{
						PushData ( );
					}
				}
			}
		}

		if ( IsBatchValid )
		{
			PushData ( );
		}

		if ( Progress.Cancelled ( ) )
		{
			return;
		}

		/** Add To Result */
		for ( const auto& BatchData : BatchDataMap )
		{
			const FVector MinPos = FVector ( BatchData.Value ) * MeshGapSize;
			const FVector MaxPos = FVector ( BatchData.Key + FIntVector ( 1 ) ) * MeshGapSize;
			const FVector Scale  = ( MaxPos - MinPos );
			const FVector Center = FMath::Lerp ( MinPos , MaxPos , 0.5f ) - MeshBoundHalfSize;

			FKBoxElem CurrentBoxElem ( Scale.X , Scale.Y , Scale.Z );

			CurrentBoxElem.Center = Center;

			ThreadData->CollisionBoxElems.Add ( CurrentBoxElem );
		}
	}

	if ( Progress.Cancelled ( ) )
	{
		return;
	}

	//if ( ThreadData->MeshData.TriangleCount ( ) != 0 && PassData.bNaniteMesh )
	//{
	//	const FDynamicMesh3& MeshData = ThreadData->MeshData;
	//
	//	Nanite::IBuilderModule::FInputMeshData InputMeshData;
	//
	//	FMeshBuildVertexData& VertexData = InputMeshData.Vertices;
	//
	//	VertexData.UVs.SetNum ( 1 );
	//
	//	{
	//		/** Vertex position */
	//		TArray < FVector3f >& MeshPosition = VertexData.Position;
	//
	//		/** Vertex normal */
	//		TArray < FVector3f >& MeshNormal = VertexData.TangentZ;
	//
	//		/** Vertex color */
	//		TArray < FColor >& MeshColor = VertexData.Color;
	//
	//		/** Vertex texture co-ordinate */
	//		TArray < FVector2f >& MeshUV0 = VertexData.UVs [ 0 ];
	//
	//		TArray < FVector3f >& TangentList   = VertexData.TangentX;
	//		TArray < FVector3f >& BiTangentList = VertexData.TangentY;
	//		TArray < uint32 >&    IndexList     = InputMeshData.TriangleIndices;
	//
	//		InputMeshData.NumTexCoords = 1; // Need Rework
	//
	//		// Section ID
	//		TArray < int32 >& MeshMaterial = InputMeshData.MaterialIndices;
	//
	//		InputMeshData.TriangleCounts.Add ( MeshData.TriangleCount ( ) );
	//
	//		const int NumTriangles  = MeshData.TriangleCount ( );
	//		const int NumVertices   = NumTriangles * 3;
	//		const int NumUVOverlays = MeshData.HasAttributes ( ) ? MeshData.Attributes ( )->NumUVLayers ( ) : 0; // One UV Support Only
	//
	//		const FDynamicMeshNormalOverlay* NormalOverlay = MeshData.HasAttributes ( ) ? MeshData.Attributes ( )->PrimaryNormals ( ) : nullptr;
	//		const FDynamicMeshColorOverlay*  ColorOverlay  = MeshData.HasAttributes ( ) ? MeshData.Attributes ( )->PrimaryColors ( ) : nullptr;
	//
	//		const FDynamicMeshMaterialAttribute* MaterialID = MeshData.HasAttributes ( ) ? MeshData.Attributes ( )->GetMaterialID ( ) : nullptr;
	//
	//		const bool bHasColor = ColorOverlay != nullptr;
	//
	//		{
	//			MeshPosition.AddUninitialized ( NumVertices );
	////////////////}

	ThreadData->WorkLenght = static_cast < int32 > ( ( FDateTime::UtcNow ( ) - WorkTime ).GetTotalMilliseconds ( ) );

	return;
}

void ULPPMarchingMeshComponent::ComputeNewMarchingMesh_Completed ( TUniquePtr < FLFPMarchingThreadData >& ThreadData , TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc >& GameThreadJob )
{
	if ( IsValid ( this ) == false )
	{
		return;
	}

	if ( ThreadData.IsValid ( ) == false )
	{
		return;
	}

	const bool bIsTaskValid = NewThreadData.IsValid ( );

	NewThreadData = MoveTemp ( ThreadData );

	if ( bIsTaskValid == false )
	{
		GameThreadJob.Enqueue ( [ this] ( )
		{
			check ( NewThreadData.IsValid ( ) );

			if ( IsValidLowLevel ( ) == false || IsValid ( this ) == false )
			{
				return;
			}

			if ( NewThreadData->DataID != ClearMeshCounter )
			{
				return;
			}

			{
				FScopeLock Lock ( &RenderDataLock );

				//const float CompactMetric = NewThreadData->MeshData.CompactMetric ( );

				//UE_LOG ( LogTemp , Warning , TEXT("Marching Data : %s") , *TempThreadData->MeshData.MeshInfoString ( ) );

				//UE_LOG ( LogTemp , Warning , TEXT("Marching Data Collision : %i") , LocalThreadData->CollisionBoxElems.Num ( ) );

				FKAggregateGeom           NewAgg;
				FLPPDynamicMeshRenderData NewRenderData;

				NewAgg.BoxElems = MoveTemp ( NewThreadData->CollisionBoxElems );

				{
					NewRenderData.MeshData      = MoveTemp ( NewThreadData->MeshData );
					NewRenderData.LumenCardData = MakeShared < FCardRepresentationData > ( );

					NewRenderData.LumenCardData->MeshCardsBuildData = MoveTemp ( NewThreadData->LumenCardData );

					if ( NewThreadData->bIsNaniteValid )
					{
						ClearNaniteResources ( NewRenderData.NaniteResourcesPtr );

						NewRenderData.NaniteResourcesPtr = MakePimpl < Nanite::FResources > ( MoveTemp ( NewThreadData->NaniteResources ) );
					}
				}

				//UE_LOG ( LogTemp , Warning , TEXT ( "Marching Data Time Use : %d ms : %i Vert Count" ) , NewThreadData->WorkLenght , NewRenderData.MeshData.VertexCount ( ) );

				SetMesh ( MoveTemp ( NewRenderData ) , MoveTemp ( NewAgg ) );
			}

			NewThreadData.Reset ( );

			OnMeshGenerated.Broadcast ( this );
		} );
	}
}

void ULPPMarchingMeshComponent::ComputeNewDistanceFieldData_Completed ( TUniquePtr < FDistanceFieldVolumeData >& NewData , TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc >& GameThreadJob )
{
	if ( IsValid ( this ) == false )
	{
		return;
	}

	const bool bIsTaskValid = NewDistanceFieldData.IsValid ( );

	NewDistanceFieldData = MoveTemp ( NewData );

	if ( bIsTaskValid == false )
	{
		GameThreadJob.Enqueue (
		                       [this] ( )
		                       {
			                       if ( IsValid ( this ) == false )
			                       {
				                       return;
			                       }

			                       FScopeLock Lock ( &RenderDataLock );

			                       if ( MeshRenderData.IsValid ( ) )
			                       {
				                       MeshRenderData->DistanceFieldPtr = MakeShareable < FDistanceFieldVolumeData > ( NewDistanceFieldData.Release ( ) );
			                       }

			                       NewDistanceFieldData.Reset ( );

			                       OnDistanceFieldGenerated.Broadcast ( this );

			                       // the new distance field will be set when the scene proxy is re-created
			                       MarkRenderStateDirty ( );
		                       }
		                      );
	}
}
