// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/LPPMarchingData.h"

#include "StaticMeshLODResourcesToDynamicMesh.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"
#include "Library/LPPMarchingFunctionLibrary.h"

void ULPPMarchingData::PostLoad ( )
{
#if WITH_EDITOR
	GenerateDynamicMeshList ( );
#endif

	Super::PostLoad ( );
}

const FDynamicMesh3* ULPPMarchingData::GetDynamicMesh ( const int32 MeshIndex ) const
{
	return DynamicMeshList.IsValidIndex ( MeshIndex ) ? &DynamicMeshList [ MeshIndex ] : nullptr;
}

FLFPMarchingMeshMappingDataV2 ULPPMarchingData::GetMappingData ( const uint8 MarchingID ) const
{
	return MappingDataList.Contains ( MarchingID )
	       ? MappingDataList.FindChecked ( MarchingID )
	       : FLFPMarchingMeshMappingDataV2 ( );
}

#if WITH_EDITOR
void ULPPMarchingData::AutoFillRotationList ( )
{
	for ( FLFPMarchingSingleMeshDataV2& MeshData : MeshDataList )
	{
		if ( MeshData.bLockRotationList )
		{
			continue;
		}

		MeshData.SupportRotationList.Empty ( );

		for ( int32 Z = 0 ; Z < 4 ; Z++ )
		{
			for ( int32 Y = 0 ; Y < 4 ; Y++ )
			{
				for ( int32 X = 0 ; X < 4 ; X++ )
				{
					const uint8 NewID = ULPPMarchingFunctionLibrary::RotateMarchingID ( MeshData.MappingID , FIntVector ( X , Y , Z ) );

					if ( MeshData.SupportRotationList.Contains ( NewID ) == false )
					{
						MeshData.SupportRotationList.Add ( NewID , FIntVector ( X , Y , Z ) );
					}
				}
			}
		}

		MeshData.SupportRotationList.KeySort ( [] ( const uint8 KeyA , const uint8 KeyB ) { return KeyA < KeyB; } );
	}
}

void ULPPMarchingData::AutoFillMappingDataList ( )
{
	GenerateDynamicMeshList ( );

	MappingDataList.Empty ( 255 );

	InvalidIDList.Empty ( 253 );
	{
		for ( int32 MeshID = 1 ; MeshID < 254 ; ++MeshID )
		{
			InvalidIDList.Add ( MeshID );
		}
	}

	DuplicateList.Empty ( );

	for ( int32 MeshID = 0 ; MeshID < MeshDataList.Num ( ) ; ++MeshID )
	{
		const FLFPMarchingSingleMeshDataV2& MeshData = MeshDataList [ MeshID ];

		for ( const auto& MeshRotation : MeshData.SupportRotationList )
		{
			if ( MappingDataList.Contains ( MeshRotation.Key ) == false )
			{
				FLFPMarchingMeshMappingDataV2& NewMappingData = MappingDataList.Add ( MeshRotation.Key );

				InvalidIDList.Remove ( MeshRotation.Key );

				NewMappingData.MeshID   = MeshID;
				NewMappingData.Rotation = MeshRotation.Value;
			}
			else
			{
				const FLFPMarchingMeshMappingDataV2& MappingData = MappingDataList.FindChecked ( MeshRotation.Key );

				DuplicateList.Add ( FIntPoint ( MappingData.MeshID , MeshID ) );
			}
		}
	}

	MappingDataList.KeySort ( [] ( const uint8 KeyA , const uint8 KeyB ) { return KeyA < KeyB; } );
}

void ULPPMarchingData::GenerateDynamicMeshList ( )
{
	DynamicMeshList.SetNum ( MeshDataList.Num ( ) );

	int32 MeshIndex = INDEX_NONE;

	DynamicMeshAmount = 0;

	for ( const FLFPMarchingSingleMeshDataV2& MeshData : MeshDataList )
	{
		MeshIndex += 1;

		const UStaticMesh* StaticMeshData = MeshData.Mesh.LoadSynchronous ( );

		if ( IsValid ( StaticMeshData ) == false )
		{
			continue;
		}

		if ( const FStaticMeshRenderData* RenderData = StaticMeshData->GetRenderData ( ) ; RenderData != nullptr )
		{
			if ( const FStaticMeshLODResources* LODData = RenderData->GetCurrentFirstLOD ( 0 ) ; LODData != nullptr )
			{
				FDynamicMesh3& NewDynamicMeshData = DynamicMeshList [ MeshIndex ];

				UE::Geometry::FStaticMeshLODResourcesToDynamicMesh::ConversionOptions ConvertOptions;

				UE::Geometry::FStaticMeshLODResourcesToDynamicMesh Converter;
				Converter.Convert ( LODData , ConvertOptions , NewDynamicMeshData );

				UE::Geometry::FMergeCoincidentMeshEdges Welder ( &NewDynamicMeshData );
				Welder.MergeVertexTolerance = 1.0f;
				Welder.OnlyUniquePairs      = false;
				Welder.Apply ( );

				NewDynamicMeshData.CompactInPlace ( nullptr );
			}
		}

		DynamicMeshAmount += 1;
	}
}
#endif
