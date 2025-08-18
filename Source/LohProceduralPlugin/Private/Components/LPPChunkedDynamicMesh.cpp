// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)


#include "Components/LPPChunkedDynamicMesh.h"

#include "Kismet/KismetMathLibrary.h"
#include "Math/BoxSphereBounds.h"


// Sets default values for this component's properties
ULPPChunkedDynamicMesh::ULPPChunkedDynamicMesh ( )
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}


// Called when the game starts
void ULPPChunkedDynamicMesh::BeginPlay ( )
{
	Super::BeginPlay ( );
}

void ULPPChunkedDynamicMesh::EndPlay ( const EEndPlayReason::Type EndPlayReason )
{
	Super::EndPlay ( EndPlayReason );

	InvalidatePhysicsData ( );
}


// Called every frame
void ULPPChunkedDynamicMesh::TickComponent ( float DeltaTime , ELevelTick TickType , FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent ( DeltaTime , TickType , ThisTickFunction );

	// ...
}

void ULPPChunkedDynamicMesh::SetMesh ( FDynamicMesh3&& MoveMesh )
{
	MeshCompactData = FLPPChunkedDynamicCompactMeshData ( );

	const FDynamicMesh3 DynamicMeshData = MoveTemp ( MoveMesh );

	if ( DynamicMeshData.TriangleCount ( ) != 0 )
	{
		const int NumTriangles  = DynamicMeshData.TriangleCount ( );
		const int NumVertices   = NumTriangles * 3;
		const int NumUVOverlays = DynamicMeshData.HasAttributes ( ) ? DynamicMeshData.Attributes ( )->NumUVLayers ( ) : 0;

		const FDynamicMeshNormalOverlay* NormalOverlay = DynamicMeshData.HasAttributes ( ) ? DynamicMeshData.Attributes ( )->PrimaryNormals ( ) : nullptr;
		const FDynamicMeshColorOverlay*  ColorOverlay  = DynamicMeshData.HasAttributes ( ) ? DynamicMeshData.Attributes ( )->PrimaryColors ( ) : nullptr;

		const bool bHasColor    = ColorOverlay != nullptr;
		const bool bHasTangents = DynamicMeshData.HasAttributes ( ) && DynamicMeshData.Attributes ( )->HasTangentSpace ( );

		UE::Geometry::FDynamicMeshTangents Tangents ( &DynamicMeshData );

		MeshCompactData = FLPPChunkedDynamicCompactMeshData ( NumVertices , bHasColor );

		// populate the triangle array.  we use this for parallelism. 
		TArray < int32 > TriangleArray;
		{
			TriangleArray.Reserve ( NumTriangles );
			for ( const int32 TriangleID : DynamicMeshData.TriangleIndicesItr ( ) )
			{
				if ( DynamicMeshData.IsTriangle ( TriangleID ) == false )
				{
					continue;
				}

				TriangleArray.Add ( TriangleID );
			}
		}

		ParallelFor ( TriangleArray.Num ( ) , [&] ( int idx )
		{
			const int32 TriangleID = TriangleArray [ idx ];

			UE::Geometry::FIndex3i TriIndexList       = DynamicMeshData.GetTriangle ( TriangleID );
			UE::Geometry::FIndex3i TriNormalIndexList = ( NormalOverlay != nullptr ) ? NormalOverlay->GetTriangle ( TriangleID ) : UE::Geometry::FIndex3i::Invalid ( );
			UE::Geometry::FIndex3i TriColorIndexList  = ( ColorOverlay != nullptr ) ? ColorOverlay->GetTriangle ( TriangleID ) : UE::Geometry::FIndex3i::Invalid ( );

			const int32 VertOffset = idx * 3;
			for ( int32 IndexOffset = 0 ; IndexOffset < 3 ; ++IndexOffset )
			{
				const int32 ListIndex     = VertOffset + IndexOffset;
				const int32 TriangleIndex = TriIndexList [ IndexOffset ];
				const int32 NormalIndex   = TriNormalIndexList [ IndexOffset ];
				const int32 ColorIndex    = TriColorIndexList [ IndexOffset ];

				MeshCompactData.Position [ ListIndex ] = static_cast < FVector3f > ( DynamicMeshData.GetVertex ( TriangleIndex ) );
				MeshCompactData.Normal [ ListIndex ]   = NormalIndex != FDynamicMesh3::InvalidID ? NormalOverlay->GetElement ( NormalIndex ) : DynamicMeshData.GetVertexNormal ( TriangleIndex );

				if ( bHasColor )
				{
					MeshCompactData.Color [ ListIndex ] = static_cast < FLinearColor > ( ColorIndex != FDynamicMesh3::InvalidID ? ColorOverlay->GetElement ( ColorIndex ) : static_cast < FVector4f > ( DynamicMeshData.GetVertexColor ( TriangleIndex ) ) ).ToFColor ( true );
				}
			}

			{
				//MeshCompactData.Normal [ idx ] = NormalIndex != FDynamicMesh3::InvalidID ? NormalOverlay->GetElement ( NormalIndex ) : DynamicMeshData.GetVertexNormal ( TriangleIndex );
				//
				//if ( bHasTangents )
				//{
				//	Tangents.GetTangentVectors ( TriangleID , IndexOffset , MeshCompactData.Normal [ ListIndex ] , MeshCompactData.Tangent [ ListIndex ] , MeshCompactData.BiTangent [ ListIndex ] );
				//}
				//else
				//{
				//	UE::Geometry::VectorUtil::MakePerpVectors ( MeshCompactData.Normal [ ListIndex ] , MeshCompactData.Tangent [ ListIndex ] , MeshCompactData.BiTangent [ ListIndex ] );
				//}
			}

			for ( int32 TexIndex = 0 ; TexIndex < 1 /*NumTexCoords */; ++TexIndex )
			{
				const FDynamicMeshUVOverlay* UVOverlay = DynamicMeshData.HasAttributes ( ) ? DynamicMeshData.Attributes ( )->GetUVLayer ( TexIndex ) : nullptr;

				UE::Geometry::FIndex3i TriUVIndexList = ( UVOverlay != nullptr ) ? UVOverlay->GetTriangle ( TriangleID ) : UE::Geometry::FIndex3i::Invalid ( );

				for ( int32 IndexOffset = 0 ; IndexOffset < 3 ; ++IndexOffset )
				{
					const int32 ListIndex = VertOffset + IndexOffset;
					const int32 UVIndex   = TriUVIndexList [ IndexOffset ];

					MeshCompactData.UV0 [ ListIndex ] = ( UVIndex != FDynamicMesh3::InvalidID ) ? UVOverlay->GetElement ( UVIndex ) : FVector2f::Zero ( );
				}
			}
		} );
	}

	LocalBounds = DynamicMeshData.GetBounds ( true );

	NotifyMeshUpdated ( DynamicMeshData );
}

void ULPPChunkedDynamicMesh::ClearMesh ( )
{
	MeshCompactData = FLPPChunkedDynamicCompactMeshData ( );

	InvalidatePhysicsData ( );

	const FDynamicMesh3 EmptyMesh;

	NotifyMeshUpdated ( EmptyMesh );
}

void ULPPChunkedDynamicMesh::NotifyMeshUpdated ( const FDynamicMesh3& MeshData )
{
	RebuildPhysicsData ( );
	MarkRenderStateDirty ( );
}

void ULPPChunkedDynamicMesh::NotifyMaterialSetUpdated ( )
{
	MarkRenderStateDirty ( );
}

bool ULPPChunkedDynamicMesh::GetTriMeshSizeEstimates ( struct FTriMeshCollisionDataEstimates& OutTriMeshEstimates , bool bInUseAllTriData ) const
{
	OutTriMeshEstimates.VerticeCount = MeshCompactData.Position.Num ( );
	return true;
}

bool ULPPChunkedDynamicMesh::GetPhysicsTriMeshData ( struct FTriMeshCollisionData* CollisionData , bool InUseAllTriData )
{
	const int32 TriCount = MeshCompactData.Position.Num ( ) / 3;

	CollisionData->Indices.Reserve ( TriCount );
	CollisionData->Vertices.Reserve ( TriCount * 3 );

	for ( int32 TriIndex = 0 ; TriIndex < TriCount ; ++TriIndex )
	{
		const int32 TriOffset = CollisionData->Indices.Num ( ) * 3;

		FTriIndices TriIndices;

		TriIndices.v0 = TriOffset;
		TriIndices.v1 = TriOffset + 1;
		TriIndices.v2 = TriOffset + 2;

		// Filter out triangles which will cause physics system to emit degenerate-geometry warnings.
		// These checks reproduce tests in Chaos::CleanTrimesh
		const FVector3f& A = MeshCompactData.Position [ TriIndex ];
		const FVector3f& B = MeshCompactData.Position [ TriIndex + 1 ];
		const FVector3f& C = MeshCompactData.Position [ TriIndex + 2 ];
		if ( A == B || A == C || B == C )
		{
			continue;
		}
		// anything that fails the first check should also fail this, but Chaos does both so doing the same here...
		const float SquaredArea = FVector3f::CrossProduct ( A - B , A - C ).SizeSquared ( );
		if ( SquaredArea < UE_SMALL_NUMBER )
		{
			continue;
		}

		CollisionData->Indices.Add ( TriIndices );
		CollisionData->Vertices.Add ( A );
		CollisionData->Vertices.Add ( B );
		CollisionData->Vertices.Add ( C );
	}

	CollisionData->MaterialIndices.Init ( 0 , CollisionData->Indices.Num ( ) );

	CollisionData->bFlipNormals    = true;
	CollisionData->bDeformableMesh = false;
	CollisionData->bFastCook       = false;

	return true;
}

bool ULPPChunkedDynamicMesh::ContainsPhysicsTriMeshData ( bool InUseAllTriData ) const
{
	return MeshCompactData.Position.Num ( ) > 2 && bEnableComplexCollision;
}

bool ULPPChunkedDynamicMesh::WantsNegXTriMesh ( )
{
	return true;
}

UBodySetup* ULPPChunkedDynamicMesh::GetBodySetup ( )
{
	if ( IsValid ( MeshBodySetup ) == false )
	{
		MeshBodySetup = CreateBodySetupHelper ( );
	}

	return MeshBodySetup;
}

void ULPPChunkedDynamicMesh::InvalidatePhysicsData ( )
{
	if ( GetBodySetup ( ) )
	{
		MeshBodySetup->AbortPhysicsMeshAsyncCreation ( );

		MeshBodySetup->RemoveSimpleCollision ( );
		DestroyPhysicsState ( );

		if ( FLPPChunkedDynamicMeshProxy* Proxy = GetBaseSceneProxy ( ) )
		{
			Proxy->SetCollisionData ( );
		}
	}
}

void ULPPChunkedDynamicMesh::RebuildPhysicsData ( )
{
	if ( IsValid ( MeshBodySetup ) == false )
	{
		MeshBodySetup = CreateBodySetupHelper ( );
	}

	MeshBodySetup->AbortPhysicsMeshAsyncCreation ( );

	if ( MeshCompactData.Position.Num ( ) > 2 )
	{
		MeshBodySetup->CreatePhysicsMeshesAsync ( FOnAsyncPhysicsCookFinished::CreateUObject ( this , &ULPPChunkedDynamicMesh::FinishPhysicsAsyncCook , MeshBodySetup.Get ( ) ) );
	}
	else
	{
		MeshBodySetup->ClearPhysicsMeshes ( );

		MeshBodySetup->AggGeom = AggGeom;

		RecreatePhysicsState ( );

		if ( FLPPChunkedDynamicMeshProxy* Proxy = GetBaseSceneProxy ( ) )
		{
			Proxy->SetCollisionData ( );
		}
	}
}

void ULPPChunkedDynamicMesh::FinishPhysicsAsyncCook ( bool bSuccess , UBodySetup* FinishedBodySetup )
{
	if ( bSuccess )
	{
		MeshBodySetup->AggGeom = AggGeom;

		RecreatePhysicsState ( );

		if ( FLPPChunkedDynamicMeshProxy* Proxy = GetBaseSceneProxy ( ) )
		{
			Proxy->SetCollisionData ( );
		}
	}
}

UBodySetup* ULPPChunkedDynamicMesh::CreateBodySetupHelper ( )
{
	UBodySetup* NewBodySetup = nullptr;
	{
		FGCScopeGuard Scope;

		// Below flags are copied from UProceduralMeshComponent::CreateBodySetupHelper(). Without these flags, DynamicMeshComponents inside
		// a DynamicMeshActor BP will result on a GLEO error after loading and modifying a saved Level (but *not* on the initial save)
		// The UBodySetup in a template needs to be public since the property is Instanced and thus is the archetype of the instance meaning there is a direct reference
		NewBodySetup = NewObject < UBodySetup > ( this , NAME_None , ( IsTemplate ( ) ? RF_Public | RF_ArchetypeObject : RF_NoFlags ) );
	}
	NewBodySetup->BodySetupGuid = FGuid::NewGuid ( );

	NewBodySetup->bGenerateMirroredCollision = false;
	NewBodySetup->CollisionTraceFlag         = CTF_UseSimpleAndComplex;

	NewBodySetup->DefaultInstance.SetCollisionProfileName ( GetCollisionProfileName ( ) );
	NewBodySetup->bSupportUVsAndFaceRemap = false; /* bSupportPhysicalMaterialMasks; */

	return NewBodySetup;
}

int32 ULPPChunkedDynamicMesh::GetNumMaterials ( ) const
{
	return BaseMaterials.Num ( );
}

UMaterialInterface* ULPPChunkedDynamicMesh::GetMaterial ( int32 ElementIndex ) const
{
	return ( ElementIndex >= 0 && ElementIndex < BaseMaterials.Num ( ) ) ? BaseMaterials [ ElementIndex ] : nullptr;
}

FMaterialRelevance ULPPChunkedDynamicMesh::GetMaterialRelevance ( ERHIFeatureLevel::Type InFeatureLevel ) const
{
	return UMeshComponent::GetMaterialRelevance ( InFeatureLevel );
}

void ULPPChunkedDynamicMesh::SetMaterial ( int32 ElementIndex , UMaterialInterface* Material )
{
	check ( ElementIndex >= 0 );
	if ( ElementIndex >= BaseMaterials.Num ( ) )
	{
		BaseMaterials.SetNum ( ElementIndex + 1 , EAllowShrinking::No );
	}
	BaseMaterials [ ElementIndex ] = Material;

	// @todo allow for precache of pipeline state objects for rendering
	// PrecachePSOs(); // indirectly calls GetUsedMaterials, requires CollectPSOPrecacheData to be implemented, see UStaticMeshComponent for example


	MarkRenderStateDirty ( );

	// update the body instance in case this material has an associated physics material 
	FBodyInstance* BodyInst = GetBodyInstance ( );
	if ( BodyInst && BodyInst->IsValidBodyInstance ( ) )
	{
		BodyInst->UpdatePhysicalMaterials ( );
	}
}

void ULPPChunkedDynamicMesh::SetNumMaterials ( int32 NumMaterials )
{
	if ( BaseMaterials.Num ( ) > NumMaterials )
	{
		// discard extra materials
		BaseMaterials.SetNum ( NumMaterials );
	}
	else
	{
		while ( NumMaterials < BaseMaterials.Num ( ) )
		{
			SetMaterial ( NumMaterials , nullptr );
			NumMaterials++;
		}
	}
}

void ULPPChunkedDynamicMesh::GetUsedMaterials ( TArray < UMaterialInterface* >& OutMaterials , bool bGetDebugMaterials ) const
{
	UMeshComponent::GetUsedMaterials ( OutMaterials , bGetDebugMaterials );
}

FLPPChunkedDynamicMeshProxy* ULPPChunkedDynamicMesh::GetBaseSceneProxy ( ) const
{
	return static_cast < FLPPChunkedDynamicMeshProxy* > ( GetSceneProxy ( ) );
}

FPrimitiveSceneProxy* ULPPChunkedDynamicMesh::CreateSceneProxy ( )
{
	// if this is not always the case, we have made incorrect assumptions
	ensure ( GetSceneProxy() == nullptr );

	FLPPChunkedDynamicMeshProxy* NewProxy = new FLPPChunkedDynamicMeshProxy ( this );

	NewProxy->Initialize ( [&] (
	                      TArray < FVector3f >& PositionList ,
	                      TArray < uint32 >&    IndexList ,
	                      TArray < FVector2f >& UV0List ,
	                      TArray < FColor >&    ColorList ,
	                      TArray < FVector3f >& NormalList ,
	                      TArray < FVector3f >& TangentList ,
	                      TArray < FVector3f >& BiTangentList
	                      )
	                      {
		                      if ( MeshCompactData.Position.IsEmpty ( ) )
		                      {
			                      return false;
		                      }

		                      PositionList = MeshCompactData.Position;
		                      UV0List      = MeshCompactData.UV0;
		                      ColorList    = MeshCompactData.Color;
		                      NormalList   = MeshCompactData.Normal;
		                      //TangentList   = MeshCompactData.Tangent;
		                      //BiTangentList = MeshCompactData.BiTangent;

		                      //NormalList.AddUninitialized ( MeshCompactData.Position.Num ( ) );
		                      TangentList.AddUninitialized ( MeshCompactData.Position.Num ( ) );
		                      BiTangentList.AddUninitialized ( MeshCompactData.Position.Num ( ) );

		                      IndexList.Reserve ( MeshCompactData.Position.Num ( ) );

		                      for ( int32 Index = 0 ; Index < MeshCompactData.Position.Num ( ) ; ++Index )
		                      {
			                      IndexList.Add ( Index );

			                      UE::Geometry::VectorUtil::MakePerpVectors ( MeshCompactData.Normal [ Index ] , TangentList [ Index ] , BiTangentList [ Index ] );
		                      }

		                      return true;
	                      }
	                     );

	return NewProxy;
}

FBoxSphereBounds ULPPChunkedDynamicMesh::CalcBounds ( const FTransform& LocalToWorld ) const
{
	// can get a tighter box by calculating in world space, but we care more about performance
	FBox             LocalBoundingBox = static_cast < FBox > ( LocalBounds );
	FBoxSphereBounds Ret ( LocalBoundingBox.TransformBy ( LocalToWorld ) );
	Ret.BoxExtent *= BoundsScale;
	Ret.SphereRadius *= BoundsScale;
	return Ret;
}
