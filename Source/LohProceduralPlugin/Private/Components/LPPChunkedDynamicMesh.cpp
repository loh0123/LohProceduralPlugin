// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)


#include "Components/LPPChunkedDynamicMesh.h"

#include "Math/BoxSphereBounds.h"
#include "PhysicsEngine/PhysicsSettings.h"

LLM_DEFINE_TAG ( LFPDynamicMesh );


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

void ULPPChunkedDynamicMesh::SetMesh ( FDynamicMesh3&& MoveMesh , FKAggregateGeom&& NewAggGeom )
{
	LLM_SCOPE_BYTAG ( LFPDynamicMesh );

	SetMeshCounter += 1;

	CurrentMeshData = MakeUnique < FDynamicMesh3 > ( MoveTemp ( MoveMesh ) );

	LocalBounds = CurrentMeshData->GetBounds ( true );

	AggGeom = MoveTemp ( NewAggGeom );

	NotifyMeshUpdated ( );
}

void ULPPChunkedDynamicMesh::ClearMesh ( )
{
	ClearMeshCounter += 1;

	CurrentMeshData.Reset ( );

	//MeshCompactData = FLPPChunkedDynamicCompactMeshData ( );

	InvalidatePhysicsData ( );

	//const FDynamicMesh3 EmptyMesh;

	NotifyMeshUpdated ( );
}

void ULPPChunkedDynamicMesh::NotifyMeshUpdated ( )
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
	OutTriMeshEstimates.VerticeCount = CurrentMeshData->VertexCount ( );
	return true;
}

bool ULPPChunkedDynamicMesh::GetPhysicsTriMeshData ( struct FTriMeshCollisionData* CollisionData , bool InUseAllTriData )
{
	if ( bEnableComplexCollision == false )
	{
		return false;
	}

	// See if we should copy UVs
	const bool bCopyUVs = UPhysicsSettings::Get ( )->bSupportUVFromHitResults && CurrentMeshData->HasAttributes ( ) && CurrentMeshData->Attributes ( )->NumUVLayers ( ) > 0;

	if ( bCopyUVs )
	{
		CollisionData->UVs.SetNum ( CurrentMeshData->Attributes ( )->NumUVLayers ( ) );
	}

	const FDynamicMeshMaterialAttribute* MaterialAttrib = CurrentMeshData->HasAttributes ( ) && CurrentMeshData->Attributes ( )->HasMaterialID ( ) ? CurrentMeshData->Attributes ( )->GetMaterialID ( ) : nullptr;

	TArray < int32 > VertexMap;
	const bool       bIsSparseV = !CurrentMeshData->IsCompactV ( );

	// copy vertices
	if ( !bCopyUVs )
	{
		if ( bIsSparseV )
		{
			VertexMap.SetNum ( CurrentMeshData->MaxVertexID ( ) );
		}
		CollisionData->Vertices.Reserve ( CurrentMeshData->VertexCount ( ) );
		for ( int32 vid : CurrentMeshData->VertexIndicesItr ( ) )
		{
			int32 Index = CollisionData->Vertices.Add ( static_cast < FVector3f > ( CurrentMeshData->GetVertex ( vid ) ) );
			if ( bIsSparseV )
			{
				VertexMap [ vid ] = Index;
			}
			else
			{
				check ( vid == Index );
			}
		}
	}
	else
	{
		// map vertices per wedge
		VertexMap.SetNumZeroed ( CurrentMeshData->MaxTriangleID ( ) * 3 );
		// temp array to store the UVs on a vertex (per triangle)
		TArray < FVector2D >            VertUVs;
		const FDynamicMeshAttributeSet* Attribs     = CurrentMeshData->Attributes ( );
		const int32                     NumUVLayers = Attribs->NumUVLayers ( );
		for ( int32 VID : CurrentMeshData->VertexIndicesItr ( ) )
		{
			FVector3f Pos       = static_cast < FVector3f > ( CurrentMeshData->GetVertex ( VID ) );
			int32     VertStart = CollisionData->Vertices.Num ( );
			CurrentMeshData->EnumerateVertexTriangles ( VID , [&] ( int32 TID )
			{
				UE::Geometry::FIndex3i Tri     = CurrentMeshData->GetTriangle ( TID );
				int32                  VSubIdx = Tri.IndexOf ( VID );
				// Get the UVs on this wedge
				VertUVs.Reset ( 8 );
				for ( int32 UVIdx = 0 ; UVIdx < NumUVLayers ; ++UVIdx )
				{
					const FDynamicMeshUVOverlay* Overlay = Attribs->GetUVLayer ( UVIdx );
					UE::Geometry::FIndex3i       UVTri   = Overlay->GetTriangle ( TID );
					int32                        ElID    = UVTri [ VSubIdx ];
					FVector2D                    UV ( 0 , 0 );
					if ( ElID >= 0 )
					{
						UV = static_cast < FVector2D > ( Overlay->GetElement ( ElID ) );
					}
					VertUVs.Add ( UV );
				}
				// Check if we've already added these UVs via an earlier wedge
				int32 OutputVIdx = INDEX_NONE;
				for ( int32 VIdx = VertStart ; VIdx < CollisionData->Vertices.Num ( ) ; ++VIdx )
				{
					bool bFound = true;
					for ( int32 UVIdx = 0 ; UVIdx < NumUVLayers ; ++UVIdx )
					{
						if ( CollisionData->UVs [ UVIdx ] [ VIdx ] != VertUVs [ UVIdx ] )
						{
							bFound = false;
							break;
						}
					}
					if ( bFound )
					{
						OutputVIdx = VIdx;
						break;
					}
				}
				// If not, add the vertex w/ the UVs
				if ( OutputVIdx == INDEX_NONE )
				{
					OutputVIdx = CollisionData->Vertices.Add ( Pos );
					for ( int32 UVIdx = 0 ; UVIdx < NumUVLayers ; ++UVIdx )
					{
						CollisionData->UVs [ UVIdx ].Add ( VertUVs [ UVIdx ] );
					}
				}
				// Map the wedge to the output vertex
				VertexMap [ TID * 3 + VSubIdx ] = OutputVIdx;
			} );
		}
	}

	// copy triangles
	CollisionData->Indices.Reserve ( CurrentMeshData->TriangleCount ( ) );
	CollisionData->MaterialIndices.Reserve ( CurrentMeshData->TriangleCount ( ) );
	for ( int32 tid : CurrentMeshData->TriangleIndicesItr ( ) )
	{
		UE::Geometry::FIndex3i Tri = CurrentMeshData->GetTriangle ( tid );
		FTriIndices            Triangle;
		if ( bCopyUVs )
		{
			// UVs need a wedge-based map
			Triangle.v0 = VertexMap [ tid * 3 + 0 ];
			Triangle.v1 = VertexMap [ tid * 3 + 1 ];
			Triangle.v2 = VertexMap [ tid * 3 + 2 ];
		}
		else if ( bIsSparseV )
		{
			Triangle.v0 = VertexMap [ Tri.A ];
			Triangle.v1 = VertexMap [ Tri.B ];
			Triangle.v2 = VertexMap [ Tri.C ];
		}
		else
		{
			Triangle.v0 = Tri.A;
			Triangle.v1 = Tri.B;
			Triangle.v2 = Tri.C;
		}

		// Filter out triangles which will cause physics system to emit degenerate-geometry warnings.
		// These checks reproduce tests in Chaos::CleanTrimesh
		const FVector3f& A = CollisionData->Vertices [ Triangle.v0 ];
		const FVector3f& B = CollisionData->Vertices [ Triangle.v1 ];
		const FVector3f& C = CollisionData->Vertices [ Triangle.v2 ];
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

		CollisionData->Indices.Add ( Triangle );

		int32 MaterialID = MaterialAttrib ? MaterialAttrib->GetValue ( tid ) : 0;
		CollisionData->MaterialIndices.Add ( MaterialID );
	}

	CollisionData->bFlipNormals    = true;
	CollisionData->bDeformableMesh = true;
	CollisionData->bFastCook       = true;

	return true;
}

bool ULPPChunkedDynamicMesh::ContainsPhysicsTriMeshData ( bool InUseAllTriData ) const
{
	return CurrentMeshData.IsValid ( ) && bEnableComplexCollision;
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

	if ( CurrentMeshData.IsValid ( ) && bEnableComplexCollision )
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
		FinishedBodySetup->AggGeom = AggGeom;

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

FMaterialRelevance ULPPChunkedDynamicMesh::GetMaterialRelevance ( EShaderPlatform InShaderPlatform ) const
{
	return UMeshComponent::GetMaterialRelevance ( InShaderPlatform );
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


	NotifyMaterialSetUpdated ( );

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
	LLM_SCOPE_BYTAG ( LFPDynamicMesh );

	// if this is not always the case, we have made incorrect assumptions
	ensure ( GetSceneProxy() == nullptr );

	FLPPChunkedDynamicMeshProxy* NewProxy = new FLPPChunkedDynamicMeshProxy ( this , DistanceFieldSelfShadowBias );

	NewProxy->InitializeFromMesh ( CurrentMeshData.Get ( ) );

	return NewProxy;
}

FBoxSphereBounds ULPPChunkedDynamicMesh::CalcBounds ( const FTransform& LocalToWorld ) const
{
	// can get a tighter box by calculating in world space, but we care more about performance
	FBox             LocalBoundingBox = static_cast < FBox > ( LocalBounds );
	FBoxSphereBounds Ret ( LocalBoundingBox.TransformBy ( LocalToWorld ) );
	Ret.BoxExtent    *= BoundsScale;
	Ret.SphereRadius *= BoundsScale;
	return Ret;
}
