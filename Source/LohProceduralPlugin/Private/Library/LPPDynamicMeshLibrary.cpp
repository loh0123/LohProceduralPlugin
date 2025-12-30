// Fill out your copyright notice in the Description page of Project Settings.


#include "Library/LPPDynamicMeshLibrary.h"

#include "DistanceFieldAtlas.h"
#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Spatial/FastWinding.h"

void ULPPDynamicMeshLibrary::BuildDynamicMeshDistanceField ( FDistanceFieldVolumeData& OutData , FProgressCancel& Progress , const UE::Geometry::FDynamicMesh3& Mesh , const bool bGenerateAsIfTwoSided , const float CurrentDistanceFieldResolutionScale )
{
	if ( Progress.Cancelled ( ) )
	{
		return;
	}

	{
		TRACE_CPUPROFILER_EVENT_SCOPE ( DynamicMesh_GenerateSignedDistanceFieldVolumeData );

		if ( DoesProjectSupportDistanceFields ( ) == false )
		{
			return;
		}

		if ( CurrentDistanceFieldResolutionScale <= 0 )
		{
			return;
		}

		const double StartTime = FPlatformTime::Seconds ( );

		const auto ComputeLinearMarchingIndex = [&] ( FIntVector MarchingCoordinate , FIntVector VolumeDimensions )
		{
			return ( MarchingCoordinate.Z * VolumeDimensions.Y + MarchingCoordinate.Y ) * VolumeDimensions.X + MarchingCoordinate.X;
		};

		UE::Geometry::FDynamicMeshAABBTree3 Spatial ( &Mesh , true ); // Find Nearby Triangle
		if ( Progress.Cancelled ( ) )
		{
			return;
		}
		UE::Geometry::FAxisAlignedBox3d                                MeshBounds = Spatial.GetBoundingBox ( );
		UE::Geometry::TFastWindingTree < UE::Geometry::FDynamicMesh3 > WindingTree ( &Spatial , true ); // Find Is Inside Or Not
		if ( Progress.Cancelled ( ) )
		{
			return;
		}

		static const auto CVar       = IConsoleManager::Get ( ).FindTConsoleVariableDataInt ( TEXT ( "r.DistanceFields.MaxPerMeshResolution" ) );
		const int32       PerMeshMax = CVar->GetValueOnAnyThread ( );

		// Meshes with an explicit artist-specified scale can go higher
		const int32 MaxNumBlocksOneDim = FMath::Min < int32 > ( FMath::DivideAndRoundNearest ( CurrentDistanceFieldResolutionScale <= 1
		                                                                                       ? PerMeshMax / 2
		                                                                                       : PerMeshMax , DistanceField::UniqueDataBrickSize ) , DistanceField::MaxIndirectionDimension - 1 );

		static const auto CVarDensity  = IConsoleManager::Get ( ).FindTConsoleVariableDataFloat ( TEXT ( "r.DistanceFields.DefaultVoxelDensity" ) );
		const float       VoxelDensity = CVarDensity->GetValueOnAnyThread ( );

		const float NumVoxelsPerLocalSpaceUnit = VoxelDensity * CurrentDistanceFieldResolutionScale;
		FBox3f      LocalSpaceMeshBounds       = FBox3f ( MeshBounds );

		// Make sure the mesh bounding box has positive extents to handle planes
		{
			FVector3f MeshBoundsCenter = LocalSpaceMeshBounds.GetCenter ( );
			FVector3f MeshBoundsExtent = FVector3f::Max ( LocalSpaceMeshBounds.GetExtent ( ) , FVector3f ( 1.0f , 1.0f , 1.0f ) );
			LocalSpaceMeshBounds.Min   = MeshBoundsCenter - MeshBoundsExtent;
			LocalSpaceMeshBounds.Max   = MeshBoundsCenter + MeshBoundsExtent;
		}

		// We sample on voxel corners and use central differencing for gradients, so a box mesh using two-sided materials whose vertices lie on LocalSpaceMeshBounds produces a zero gradient on intersection
		// Expand the mesh bounds by a fraction of a voxel to allow room for a pullback on the hit location for computing the gradient.
		// Only expand for two-sided meshes as this adds significant Mesh SDF tracing cost
		if ( bGenerateAsIfTwoSided )
		{
			const FVector3f  DesiredDimensions         = FVector3f ( LocalSpaceMeshBounds.GetSize ( ) * FVector3f ( NumVoxelsPerLocalSpaceUnit / static_cast < float > ( DistanceField::UniqueDataBrickSize ) ) );
			const FIntVector Mip0IndirectionDimensions = FIntVector (
			                                                         FMath::Clamp ( FMath::RoundToInt ( DesiredDimensions.X ) , 1 , MaxNumBlocksOneDim ) ,
			                                                         FMath::Clamp ( FMath::RoundToInt ( DesiredDimensions.Y ) , 1 , MaxNumBlocksOneDim ) ,
			                                                         FMath::Clamp ( FMath::RoundToInt ( DesiredDimensions.Z ) , 1 , MaxNumBlocksOneDim ) );

			constexpr float CentralDifferencingExpandInVoxels = .25f;
			const FVector3f TexelObjectSpaceSize              = LocalSpaceMeshBounds.GetSize ( ) / FVector3f ( Mip0IndirectionDimensions * DistanceField::UniqueDataBrickSize - FIntVector ( 2 * CentralDifferencingExpandInVoxels ) );
			LocalSpaceMeshBounds                              = LocalSpaceMeshBounds.ExpandBy ( TexelObjectSpaceSize );
		}

		// The tracing shader uses a Volume space normalized by the maximum extent. To keep Volume space within [-1, 1], we must match that behavior when encoding
		const float LocalToVolumeScale = 1.0f / LocalSpaceMeshBounds.GetExtent ( ).GetMax ( );

		const FVector3f  DesiredDimensions         = FVector3f ( LocalSpaceMeshBounds.GetSize ( ) * FVector3f ( NumVoxelsPerLocalSpaceUnit / static_cast < float > ( DistanceField::UniqueDataBrickSize ) ) );
		const FIntVector Mip0IndirectionDimensions = FIntVector (
		                                                         FMath::Clamp ( FMath::RoundToInt ( DesiredDimensions.X ) , 1 , MaxNumBlocksOneDim ) ,
		                                                         FMath::Clamp ( FMath::RoundToInt ( DesiredDimensions.Y ) , 1 , MaxNumBlocksOneDim ) ,
		                                                         FMath::Clamp ( FMath::RoundToInt ( DesiredDimensions.Z ) , 1 , MaxNumBlocksOneDim ) );

		TArray < uint8 > StreamableMipData;

		struct FDistanceFieldBrick
		{
			FDistanceFieldBrick (
				float         InLocalSpaceTraceDistance ,
				const FBox3f& InVolumeBounds ,
				float         InLocalToVolumeScale ,
				FVector2f     InDistanceFieldToVolumeScaleBias ,
				FIntVector    InBrickCoordinate ,
				FIntVector    InIndirectionSize ) : LocalSpaceTraceDistance ( InLocalSpaceTraceDistance ),
				                                 VolumeBounds ( InVolumeBounds ),
				                                 LocalToVolumeScale ( InLocalToVolumeScale ),
				                                 DistanceFieldToVolumeScaleBias ( InDistanceFieldToVolumeScaleBias ),
				                                 BrickCoordinate ( InBrickCoordinate ),
				                                 IndirectionSize ( InIndirectionSize ),
				                                 BrickMaxDistance ( MIN_uint8 ),
				                                 BrickMinDistance ( MAX_uint8 )
			{
			}

			float      LocalSpaceTraceDistance;
			FBox3f     VolumeBounds;
			float      LocalToVolumeScale;
			FVector2f  DistanceFieldToVolumeScaleBias;
			FIntVector BrickCoordinate;
			FIntVector IndirectionSize;

			// Output
			uint8            BrickMaxDistance;
			uint8            BrickMinDistance;
			TArray < uint8 > DistanceFieldVolume;
		};

		for ( int32 MipIndex = 0 ; MipIndex < DistanceField::NumMips ; MipIndex++ )
		{
			if ( Progress.Cancelled ( ) )
			{
				return;
			}

			const FIntVector IndirectionDimensions = FIntVector (
			                                                     FMath::DivideAndRoundUp ( Mip0IndirectionDimensions.X , 1 << MipIndex ) ,
			                                                     FMath::DivideAndRoundUp ( Mip0IndirectionDimensions.Y , 1 << MipIndex ) ,
			                                                     FMath::DivideAndRoundUp ( Mip0IndirectionDimensions.Z , 1 << MipIndex ) );

			// Expand to guarantee one voxel border for gradient reconstruction using bilinear filtering
			const FVector3f TexelObjectSpaceSize      = LocalSpaceMeshBounds.GetSize ( ) / FVector3f ( IndirectionDimensions * DistanceField::UniqueDataBrickSize - FIntVector ( 2 * DistanceField::MeshDistanceFieldObjectBorder ) );
			const FBox3f    DistanceFieldVolumeBounds = LocalSpaceMeshBounds.ExpandBy ( TexelObjectSpaceSize );

			const FVector3f IndirectionMarchingSize = DistanceFieldVolumeBounds.GetSize ( ) / FVector3f ( IndirectionDimensions );

			const FVector3f VolumeSpaceDistanceFieldMarchingSize = IndirectionMarchingSize * LocalToVolumeScale / FVector3f ( DistanceField::UniqueDataBrickSize );
			const float     MaxDistanceForEncoding               = VolumeSpaceDistanceFieldMarchingSize.Size ( ) * DistanceField::BandSizeInVoxels;
			const float     LocalSpaceTraceDistance              = MaxDistanceForEncoding / LocalToVolumeScale;
			const FVector2f DistanceFieldToVolumeScaleBias ( 2.0f * MaxDistanceForEncoding , -MaxDistanceForEncoding );

			TArray < FDistanceFieldBrick > BricksToCompute;
			BricksToCompute.Reserve ( IndirectionDimensions.X * IndirectionDimensions.Y * IndirectionDimensions.Z / 8 );
			for ( int32 ZIndex = 0 ; ZIndex < IndirectionDimensions.Z ; ZIndex++ )
			{
				for ( int32 YIndex = 0 ; YIndex < IndirectionDimensions.Y ; YIndex++ )
				{
					for ( int32 XIndex = 0 ; XIndex < IndirectionDimensions.X ; XIndex++ )
					{
						BricksToCompute.Emplace (
						                         LocalSpaceTraceDistance ,
						                         DistanceFieldVolumeBounds ,
						                         LocalToVolumeScale ,
						                         DistanceFieldToVolumeScaleBias ,
						                         FIntVector ( XIndex , YIndex , ZIndex ) ,
						                         IndirectionDimensions );
					}
				}
			}

			if ( Progress.Cancelled ( ) )
			{
				return;
			}

			// compute bricks now
			ParallelFor ( BricksToCompute.Num ( ) , [&] ( const int32 BrickIndex )
			{
				FDistanceFieldBrick& Brick                        = BricksToCompute [ BrickIndex ];
				const FVector3f      BrickIndirectionMarchingSize = Brick.VolumeBounds.GetSize ( ) / FVector3f ( Brick.IndirectionSize );
				const FVector3f      DistanceFieldMarchingSize    = BrickIndirectionMarchingSize / FVector3f ( DistanceField::UniqueDataBrickSize );
				const FVector3f      BrickMinPosition             = Brick.VolumeBounds.Min + FVector3f ( Brick.BrickCoordinate ) * BrickIndirectionMarchingSize;

				Brick.DistanceFieldVolume.Empty ( DistanceField::BrickSize * DistanceField::BrickSize * DistanceField::BrickSize );
				Brick.DistanceFieldVolume.AddZeroed ( DistanceField::BrickSize * DistanceField::BrickSize * DistanceField::BrickSize );

				for ( int32 ZIndex = 0 ; ZIndex < DistanceField::BrickSize ; ZIndex++ )
				{
					if ( Progress.Cancelled ( ) ) { return; }

					for ( int32 YIndex = 0 ; YIndex < DistanceField::BrickSize ; YIndex++ )
					{
						if ( Progress.Cancelled ( ) ) { return; }

						for ( int32 XIndex = 0 ; XIndex < DistanceField::BrickSize ; XIndex++ )
						{
							const FVector3f MarchingPosition = FVector3f ( XIndex , YIndex , ZIndex ) * DistanceFieldMarchingSize + BrickMinPosition;
							const int32     Index            = ( ZIndex * DistanceField::BrickSize * DistanceField::BrickSize + YIndex * DistanceField::BrickSize + XIndex );

							float MinLocalSpaceDistance = LocalSpaceTraceDistance;

							double NearestDistSqr    = 0;
							int32  NearestTriangleID = Spatial.FindNearestTriangle ( FVector3d ( MarchingPosition ) , NearestDistSqr ,
							                                                        UE::Geometry::IMeshSpatial::FQueryOptions ( LocalSpaceTraceDistance ) );
							if ( NearestTriangleID != IndexConstants::InvalidID )
							{
								const float ClosestDistance = FMath::Sqrt ( NearestDistSqr );
								MinLocalSpaceDistance       = FMath::Min ( MinLocalSpaceDistance , ClosestDistance );

								if ( WindingTree.IsInside ( FVector3d ( MarchingPosition ) , 0.5 ) )
								{
									MinLocalSpaceDistance *= -1;
								}
							}
							else
							{
								// no closest point...
								MinLocalSpaceDistance = LocalSpaceTraceDistance;
							}

							// Transform to the tracing shader's Volume space
							const float VolumeSpaceDistance = MinLocalSpaceDistance * LocalToVolumeScale;
							// Transform to the Distance Field texture's space
							const float RescaledDistance = ( VolumeSpaceDistance - DistanceFieldToVolumeScaleBias.Y ) / DistanceFieldToVolumeScaleBias.X;
							check ( DistanceField::DistanceFieldFormat == PF_G8 );
							const uint8 QuantizedDistance       = FMath::Clamp < int32 > ( FMath::FloorToInt ( RescaledDistance * 255.0f + .5f ) , 0 , 255 );
							Brick.DistanceFieldVolume [ Index ] = QuantizedDistance;
							Brick.BrickMaxDistance              = FMath::Max ( Brick.BrickMaxDistance , QuantizedDistance );
							Brick.BrickMinDistance              = FMath::Min ( Brick.BrickMinDistance , QuantizedDistance );
						}                        // X iteration 
					}                            // Y iteration
				}                                // Z iteration
			} , EParallelForFlags::Unbalanced ); // Bricks iteration

			if ( Progress.Cancelled ( ) )
			{
				return;
			}

			FSparseDistanceFieldMip& OutMip = OutData.Mips [ MipIndex ];
			TArray < uint32 >        IndirectionTable;
			IndirectionTable.Empty ( IndirectionDimensions.X * IndirectionDimensions.Y * IndirectionDimensions.Z );
			IndirectionTable.AddUninitialized ( IndirectionDimensions.X * IndirectionDimensions.Y * IndirectionDimensions.Z );

			for ( int32 i = 0 ; i < IndirectionTable.Num ( ) ; i++ )
			{
				IndirectionTable [ i ] = DistanceField::InvalidBrickIndex;
			}

			TArray < FDistanceFieldBrick* > ValidBricks;
			ValidBricks.Reserve ( BricksToCompute.Num ( ) );

			for ( int32 k = 0 ; k < BricksToCompute.Num ( ) ; k++ )
			{
				const FDistanceFieldBrick& ComputedBrick = BricksToCompute [ k ];
				if ( ComputedBrick.BrickMinDistance < MAX_uint8 && ComputedBrick.BrickMaxDistance > MIN_uint8 )
				{
					ValidBricks.Add ( &BricksToCompute [ k ] );
				}
			}

			const uint32 NumBricks      = ValidBricks.Num ( );
			const uint32 BrickSizeBytes = DistanceField::BrickSize * DistanceField::BrickSize * DistanceField::BrickSize * GPixelFormats [ DistanceField::DistanceFieldFormat ].BlockBytes;

			TArray < uint8 > DistanceFieldBrickData;
			DistanceFieldBrickData.Empty ( BrickSizeBytes * NumBricks );
			DistanceFieldBrickData.AddUninitialized ( BrickSizeBytes * NumBricks );

			if ( Progress.Cancelled ( ) )
			{
				return;
			}

			for ( int32 BrickIndex = 0 ; BrickIndex < ValidBricks.Num ( ) ; BrickIndex++ )
			{
				const FDistanceFieldBrick& Brick            = *ValidBricks [ BrickIndex ];
				const int32                IndirectionIndex = ComputeLinearMarchingIndex ( Brick.BrickCoordinate , IndirectionDimensions );
				IndirectionTable [ IndirectionIndex ]       = BrickIndex;

				check ( BrickSizeBytes == Brick.DistanceFieldVolume.Num() * Brick.DistanceFieldVolume.GetTypeSize() );
				FPlatformMemory::Memcpy ( &DistanceFieldBrickData [ BrickIndex * BrickSizeBytes ] , Brick.DistanceFieldVolume.GetData ( ) , Brick.DistanceFieldVolume.Num ( ) * Brick.DistanceFieldVolume.GetTypeSize ( ) );
			}

			const int32 IndirectionTableBytes = IndirectionTable.Num ( ) * IndirectionTable.GetTypeSize ( );
			const int32 MipDataBytes          = IndirectionTableBytes + DistanceFieldBrickData.Num ( );

			if ( MipIndex == DistanceField::NumMips - 1 )
			{
				OutData.AlwaysLoadedMip.Empty ( MipDataBytes );
				OutData.AlwaysLoadedMip.AddUninitialized ( MipDataBytes );

				FPlatformMemory::Memcpy ( &OutData.AlwaysLoadedMip [ 0 ] , IndirectionTable.GetData ( ) , IndirectionTableBytes );

				if ( DistanceFieldBrickData.Num ( ) > 0 )
				{
					FPlatformMemory::Memcpy ( &OutData.AlwaysLoadedMip [ IndirectionTableBytes ] , DistanceFieldBrickData.GetData ( ) , DistanceFieldBrickData.Num ( ) );
				}
			}
			else
			{
				OutMip.BulkOffset = StreamableMipData.Num ( );
				StreamableMipData.AddUninitialized ( MipDataBytes );
				OutMip.BulkSize = StreamableMipData.Num ( ) - OutMip.BulkOffset;
				checkf ( OutMip.BulkSize > 0 , TEXT("MarchingDynamicMeshComponent - BulkSize was 0 with %ux%ux%u indirection") , IndirectionDimensions.X , IndirectionDimensions.Y , IndirectionDimensions.Z );

				FPlatformMemory::Memcpy ( &StreamableMipData [ OutMip.BulkOffset ] , IndirectionTable.GetData ( ) , IndirectionTableBytes );

				if ( DistanceFieldBrickData.Num ( ) > 0 )
				{
					FPlatformMemory::Memcpy ( &StreamableMipData [ OutMip.BulkOffset + IndirectionTableBytes ] , DistanceFieldBrickData.GetData ( ) , DistanceFieldBrickData.Num ( ) );
				}
			}

			if ( Progress.Cancelled ( ) )
			{
				return;
			}

			OutMip.IndirectionDimensions          = IndirectionDimensions;
			OutMip.DistanceFieldToVolumeScaleBias = DistanceFieldToVolumeScaleBias;
			OutMip.NumDistanceFieldBricks         = NumBricks;

			// Account for the border voxels we added
			const FVector3f VirtualUVMin  = FVector3f ( DistanceField::MeshDistanceFieldObjectBorder ) / FVector3f ( IndirectionDimensions * DistanceField::UniqueDataBrickSize );
			const FVector3f VirtualUVSize = FVector3f ( IndirectionDimensions * DistanceField::UniqueDataBrickSize - FIntVector ( 2 * DistanceField::MeshDistanceFieldObjectBorder ) ) / FVector3f ( IndirectionDimensions * DistanceField::UniqueDataBrickSize );

			const FVector3f VolumePositionExtent = LocalSpaceMeshBounds.GetExtent ( ) * LocalToVolumeScale;

			// [-VolumePositionExtent, VolumePositionExtent] -> [VirtualUVMin, VirtualUVMin + VirtualUVSize]
			OutMip.VolumeToVirtualUVScale = VirtualUVSize / ( 2 * VolumePositionExtent );
			OutMip.VolumeToVirtualUVAdd   = VolumePositionExtent * OutMip.VolumeToVirtualUVScale + VirtualUVMin;
		}

		OutData.bMostlyTwoSided      = bGenerateAsIfTwoSided;
		OutData.LocalSpaceMeshBounds = LocalSpaceMeshBounds;

		if ( Progress.Cancelled ( ) )
		{
			return;
		}

		OutData.StreamableMips.Lock ( LOCK_READ_WRITE );
		uint8* Ptr = ( uint8* ) OutData.StreamableMips.Realloc ( StreamableMipData.Num ( ) );
		FMemory::Memcpy ( Ptr , StreamableMipData.GetData ( ) , StreamableMipData.Num ( ) );
		OutData.StreamableMips.Unlock ( );
		OutData.StreamableMips.SetBulkDataFlags ( BULKDATA_Force_NOT_InlinePayload );

		const float BuildTime = static_cast < float > ( FPlatformTime::Seconds ( ) - StartTime );

		if ( BuildTime > 1.0f )
			UE_LOG ( LogGeometry , Log , TEXT("LPPDynamicMeshLibrary - Finished distance field build in %.1fs - %ux%ux%u sparse distance field, %.1fMb total, %.1fMb always loaded, %u%% occupied, %u triangles") ,
		         BuildTime ,
		         Mip0IndirectionDimensions.X * DistanceField::UniqueDataBrickSize ,
		         Mip0IndirectionDimensions.Y * DistanceField::UniqueDataBrickSize ,
		         Mip0IndirectionDimensions.Z * DistanceField::UniqueDataBrickSize ,
		         (OutData.GetResourceSizeBytes() + OutData.StreamableMips.GetBulkDataSize()) / 1024.0f / 1024.0f ,
		         (OutData.AlwaysLoadedMip.GetAllocatedSize()) / 1024.0f / 1024.0f ,
		         FMath::RoundToInt(100.0f * OutData.Mips[0].NumDistanceFieldBricks / (float)(Mip0IndirectionDimensions.X * Mip0IndirectionDimensions.Y * Mip0IndirectionDimensions.Z)) ,
		         Mesh.TriangleCount() );

		if ( Progress.Cancelled ( ) )
		{
			return;
		}
	}

	return;
}
