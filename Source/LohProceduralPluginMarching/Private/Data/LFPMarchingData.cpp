// Fill out your copyright notice in the Description page of Project Settings.

#include "Data/LPPMarchingData.h"

#include "StaticMeshLODResourcesToDynamicMesh.h"
#include "DynamicMesh/Operations/MergeCoincidentMeshEdges.h"
#include "Library/LPPMarchingFunctionLibrary.h"

void ULPPMarchingData::PostLoad ( )
{
	GenerateDynamicMeshList ( );

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

#endif

void ULPPMarchingData::GenerateDynamicMeshList ( )
{
#if WITH_EDITORONLY_DATA
	DynamicMeshList.SetNum ( MeshDataList.Num ( ) );
	StaticMeshList.Reset ( MeshDataList.Num ( ) );
	StaticMeshList.SetNum ( MeshDataList.Num ( ) );

	DynamicMeshAmount = 0;

	for ( int32 MeshDataIndex = 0 ; MeshDataIndex < MeshDataList.Num ( ) ; ++MeshDataIndex )
	{
		const FLFPMarchingSingleMeshDataV2& MeshData = MeshDataList [ MeshDataIndex ];

		UStaticMesh* StaticMeshData = MeshData.Mesh.LoadSynchronous ( );

		if ( IsValid ( StaticMeshData ) == false )
		{
			continue;
		}

		if ( const FStaticMeshRenderData* RenderData = StaticMeshData->GetRenderData ( ) ; RenderData != nullptr )
		{
			if ( const FStaticMeshLODResources* LODData = RenderData->GetCurrentFirstLOD ( 0 ) ; LODData != nullptr )
			{
				FDynamicMesh3& NewDynamicMeshData = DynamicMeshList [ MeshDataIndex ];

				UE::Geometry::FStaticMeshLODResourcesToDynamicMesh::ConversionOptions ConvertOptions;

				UE::Geometry::FStaticMeshLODResourcesToDynamicMesh Converter;
				Converter.Convert ( LODData , ConvertOptions , NewDynamicMeshData );

				UE::Geometry::FMergeCoincidentMeshEdges Welder ( &NewDynamicMeshData );
				Welder.MergeVertexTolerance = 1.0f;
				Welder.OnlyUniquePairs      = false;
				Welder.Apply ( );

				NewDynamicMeshData.CompactInPlace ( nullptr );

				StaticMeshList [ MeshDataIndex ] = StaticMeshData;

				DynamicMeshAmount += 1;
			}
		}
	}
#else
	if ( StaticMeshList.IsEmpty ( ) == false )
	{
		DynamicMeshList.SetNum ( StaticMeshList.Num ( ) );

		for ( int32 MeshDataIndex = 0 ; MeshDataIndex < StaticMeshList.Num ( ) ; ++MeshDataIndex )
		{
			UStaticMesh* StaticMeshData = StaticMeshList [ MeshDataIndex ].LoadSynchronous ( );

			if ( IsValid ( StaticMeshData ) == false )
			{
				continue;
			}

			if ( const FStaticMeshRenderData* RenderData = StaticMeshData->GetRenderData ( ) ; RenderData != nullptr )
			{
				if ( const FStaticMeshLODResources* LODData = RenderData->GetCurrentFirstLOD ( 0 ) ; LODData != nullptr )
				{
					FDynamicMesh3& NewDynamicMeshData = DynamicMeshList [ MeshDataIndex ];

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
		}
	}
#endif
}
