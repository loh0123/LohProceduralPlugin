// Copyright (c) 2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)


#include "Components/LPPDynamicMesh.h"

#include "Data/LPPDynamicMeshRenderData.h"
#include "Math/BoxSphereBounds.h"
#include "PhysicsEngine/PhysicsSettings.h"

LLM_DEFINE_TAG ( LFPDynamicMesh );

/*------------------------------------------------------------------------------
FLPPDynamicMeshRenderData
------------------------------------------------------------------------------*/

/*-----------------------------------------------------------------------------
ULPPDynamicMesh
-----------------------------------------------------------------------------*/

// Sets default values for this component's properties
ULPPDynamicMesh::ULPPDynamicMesh ( )
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = false;

	// ...
}


// Called when the game starts
void ULPPDynamicMesh::BeginPlay ( )
{
	Super::BeginPlay ( );
}

void ULPPDynamicMesh::EndPlay ( const EEndPlayReason::Type EndPlayReason )
{
	Super::EndPlay ( EndPlayReason );

	DestroyPhysicsState ( );

	if ( MeshRenderData.IsValid ( ) && MeshRenderData->IsInitialized ( ) )
	{
		MeshRenderData->ReleaseResources ( );
	}
}


// Called every frame
void ULPPDynamicMesh::TickComponent ( float DeltaTime , ELevelTick TickType , FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent ( DeltaTime , TickType , ThisTickFunction );

	// ...
}

void ULPPDynamicMesh::SetMesh ( FLPPDynamicMeshRenderData&& MoveData , FKAggregateGeom&& NewAggGeom )
{
	LLM_SCOPE_BYTAG ( LFPDynamicMesh );

	SetMeshCounter += 1;

	if ( MoveData.MeshData.TriangleCount ( ) == 0 )
	{
		if ( MeshRenderData.IsValid ( ) )
		{
			MeshRenderData->ReleaseResources ( );

			ReleaseResourcesFence.BeginFence ( );

			ReleaseResourcesFence.Wait ( ); // Flush Render

			MeshRenderData.Reset ( );
		}
	}
	else
	{
		MeshRenderData = MakeShared < FLPPDynamicMeshRenderData > ( MoveTemp ( MoveData ) );

		MeshRenderData->LocalBounds = MeshRenderData->MeshData.GetBounds ( true );

		AggGeom = MoveTemp ( NewAggGeom );

		UWorld* World = GetWorld ( );
		MeshRenderData->InitResources ( World != nullptr ? World->GetFeatureLevel ( ) : ERHIFeatureLevel::Num , this );

		ReleaseResourcesFence.BeginFence ( );
	}

	NotifyMeshUpdated ( );
}

void ULPPDynamicMesh::ClearMesh ( )
{
	ClearMeshCounter += 1;

	if ( MeshRenderData.IsValid ( ) )
	{
		MeshRenderData->ReleaseResources ( );

		ReleaseResourcesFence.BeginFence ( );

		ReleaseResourcesFence.Wait ( ); // Flush Render

		MeshRenderData.Reset ( );
	}

	//MeshCompactData = FLPPChunkedDynamicCompactMeshData ( );

	InvalidatePhysicsData ( );

	//const FDynamicMesh3 EmptyMesh;

	NotifyMeshUpdated ( );
}

void ULPPDynamicMesh::NotifyMeshUpdated ( )
{
	RebuildPhysicsData ( );
	MarkRenderStateDirty ( );
}

void ULPPDynamicMesh::NotifyMaterialSetUpdated ( )
{
	MarkRenderStateDirty ( );
}

bool ULPPDynamicMesh::GetTriMeshSizeEstimates ( struct FTriMeshCollisionDataEstimates& OutTriMeshEstimates , bool bInUseAllTriData ) const
{
	OutTriMeshEstimates.VerticeCount = MeshRenderData->MeshData.VertexCount ( );
	return true;
}

bool ULPPDynamicMesh::GetPhysicsTriMeshData ( struct FTriMeshCollisionData* CollisionData , bool InUseAllTriData )
{
	if ( bEnableComplexCollision == false )
	{
		return false;
	}

	// See if we should copy UVs
	const bool bCopyUVs = UPhysicsSettings::Get ( )->bSupportUVFromHitResults && MeshRenderData->MeshData.HasAttributes ( ) && MeshRenderData->MeshData.Attributes ( )->NumUVLayers ( ) > 0;

	if ( bCopyUVs )
	{
		CollisionData->UVs.SetNum ( MeshRenderData->MeshData.Attributes ( )->NumUVLayers ( ) );
	}

	const FDynamicMeshMaterialAttribute* MaterialAttrib = MeshRenderData->MeshData.HasAttributes ( ) && MeshRenderData->MeshData.Attributes ( )->HasMaterialID ( ) ? MeshRenderData->MeshData.Attributes ( )->GetMaterialID ( ) : nullptr;

	TArray < int32 > VertexMap;
	const bool       bIsSparseV = !MeshRenderData->MeshData.IsCompactV ( );

	// copy vertices
	if ( !bCopyUVs )
	{
		if ( bIsSparseV )
		{
			VertexMap.SetNum ( MeshRenderData->MeshData.MaxVertexID ( ) );
		}
		CollisionData->Vertices.Reserve ( MeshRenderData->MeshData.VertexCount ( ) );
		for ( int32 vid : MeshRenderData->MeshData.VertexIndicesItr ( ) )
		{
			int32 Index = CollisionData->Vertices.Add ( static_cast < FVector3f > ( MeshRenderData->MeshData.GetVertex ( vid ) ) );
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
		VertexMap.SetNumZeroed ( MeshRenderData->MeshData.MaxTriangleID ( ) * 3 );
		// temp array to store the UVs on a vertex (per triangle)
		TArray < FVector2D >            VertUVs;
		const FDynamicMeshAttributeSet* Attribs     = MeshRenderData->MeshData.Attributes ( );
		const int32                     NumUVLayers = Attribs->NumUVLayers ( );
		for ( int32 VID : MeshRenderData->MeshData.VertexIndicesItr ( ) )
		{
			FVector3f Pos       = static_cast < FVector3f > ( MeshRenderData->MeshData.GetVertex ( VID ) );
			int32     VertStart = CollisionData->Vertices.Num ( );
			MeshRenderData->MeshData.EnumerateVertexTriangles ( VID , [&] ( int32 TID )
			{
				UE::Geometry::FIndex3i Tri     = MeshRenderData->MeshData.GetTriangle ( TID );
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
	CollisionData->Indices.Reserve ( MeshRenderData->MeshData.TriangleCount ( ) );
	CollisionData->MaterialIndices.Reserve ( MeshRenderData->MeshData.TriangleCount ( ) );
	for ( int32 tid : MeshRenderData->MeshData.TriangleIndicesItr ( ) )
	{
		UE::Geometry::FIndex3i Tri = MeshRenderData->MeshData.GetTriangle ( tid );
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

bool ULPPDynamicMesh::ContainsPhysicsTriMeshData ( bool InUseAllTriData ) const
{
	return MeshRenderData.IsValid ( ) && bEnableComplexCollision;
}

bool ULPPDynamicMesh::WantsNegXTriMesh ( )
{
	return true;
}

UBodySetup* ULPPDynamicMesh::GetBodySetup ( )
{
	if ( IsValid ( MeshBodySetup ) == false )
	{
		MeshBodySetup = CreateBodySetupHelper ( );
	}

	return MeshBodySetup;
}

void ULPPDynamicMesh::InvalidatePhysicsData ( )
{
	if ( IsValid ( GetBodySetup ( ) ) )
	{
		MeshBodySetup->AbortPhysicsMeshAsyncCreation ( );

		MeshBodySetup->RemoveSimpleCollision ( );
		DestroyPhysicsState ( );

		if ( FLPPChunkedDynamicMeshProxy* Proxy = GetBaseSceneProxy ( ) ; Proxy != nullptr )
		{
			Proxy->SetCollisionData ( );
		}
	}
}

void ULPPDynamicMesh::RebuildPhysicsData ( )
{
	if ( IsValid ( MeshBodySetup ) == false )
	{
		MeshBodySetup = CreateBodySetupHelper ( );
	}

	MeshBodySetup->AbortPhysicsMeshAsyncCreation ( );

	if ( MeshRenderData.IsValid ( ) && bEnableComplexCollision )
	{
		MeshBodySetup->CreatePhysicsMeshesAsync ( FOnAsyncPhysicsCookFinished::CreateUObject ( this , &ULPPDynamicMesh::FinishPhysicsAsyncCook , MeshBodySetup.Get ( ) ) );
	}
	else
	{
		MeshBodySetup->ClearPhysicsMeshes ( );

		MeshBodySetup->AggGeom = AggGeom;

		RecreatePhysicsState ( );

		if ( FLPPChunkedDynamicMeshProxy* Proxy = GetBaseSceneProxy ( ) ; Proxy != nullptr )
		{
			Proxy->SetCollisionData ( );
		}
	}
}

void ULPPDynamicMesh::FinishPhysicsAsyncCook ( bool bSuccess , UBodySetup* FinishedBodySetup )
{
	if ( bSuccess )
	{
		FinishedBodySetup->AggGeom = AggGeom;

		RecreatePhysicsState ( );

		if ( FLPPChunkedDynamicMeshProxy* Proxy = GetBaseSceneProxy ( ) ; Proxy != nullptr )
		{
			Proxy->SetCollisionData ( );
		}
	}
}

UBodySetup* ULPPDynamicMesh::CreateBodySetupHelper ( )
{
	UBodySetup* NewBodySetup;
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

int32 ULPPDynamicMesh::GetNumMaterials ( ) const
{
	return BaseMaterials.Num ( );
}

UMaterialInterface* ULPPDynamicMesh::GetMaterial ( int32 ElementIndex ) const
{
	return ( ElementIndex >= 0 && ElementIndex < BaseMaterials.Num ( ) ) ? BaseMaterials [ ElementIndex ] : nullptr;
}

FMaterialRelevance ULPPDynamicMesh::GetMaterialRelevance ( EShaderPlatform InShaderPlatform ) const
{
	return UMeshComponent::GetMaterialRelevance ( InShaderPlatform );
}

void ULPPDynamicMesh::SetMaterial ( int32 ElementIndex , UMaterialInterface* Material )
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

void ULPPDynamicMesh::SetNumMaterials ( int32 NumMaterials )
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

void ULPPDynamicMesh::GetUsedMaterials ( TArray < UMaterialInterface* >& OutMaterials , bool bGetDebugMaterials ) const
{
	UMeshComponent::GetUsedMaterials ( OutMaterials , bGetDebugMaterials );
}

void ULPPDynamicMesh::GetResourceSizeEx ( FResourceSizeEx& CumulativeResourceSize )
{
	Super::GetResourceSizeEx ( CumulativeResourceSize );

	if ( GetRenderData ( ).IsValid ( ) )
	{
		GetRenderData ( )->GetResourceSizeEx ( CumulativeResourceSize );
	}
}

float ULPPDynamicMesh::GetDistanceFieldSelfShadowBias ( ) const
{
	return DistanceFieldSelfShadowBias;
}

FLPPChunkedDynamicMeshProxy* ULPPDynamicMesh::GetBaseSceneProxy ( ) const
{
	return static_cast < FLPPChunkedDynamicMeshProxy* > ( GetSceneProxy ( ) );
}

FPrimitiveSceneProxy* ULPPDynamicMesh::CreateSceneProxy ( )
{
	LLM_SCOPE_BYTAG ( LFPDynamicMesh );

	if ( MeshRenderData.IsValid ( ) == false )
	{
		return nullptr;
	}

	// if this is not always the case, we have made incorrect assumptions
	ensure ( GetSceneProxy() == nullptr );

	// Wait For The Data To Commit To Render
	ReleaseResourcesFence.Wait ( );

	//if ( MeshRenderData->HasValidNaniteData ( ) )
	//{
	//	check ( MeshRenderData->NaniteResourcesPtr->RuntimeResourceID != INDEX_NONE && MeshRenderData->NaniteResourcesPtr->HierarchyOffset != INDEX_NONE );
	//
	//	FLPPNaniteChunkedDynamicMeshProxy* NewProxy = new FLPPNaniteChunkedDynamicMeshProxy ( this );
	//
	//	return NewProxy;
	//}
	//else
	{
		FLPPChunkedDynamicMeshProxy* NewProxy = new FLPPChunkedDynamicMeshProxy ( this , DistanceFieldSelfShadowBias );

		return NewProxy;
	}

	return nullptr;
}

FBoxSphereBounds ULPPDynamicMesh::CalcBounds ( const FTransform& LocalToWorld ) const
{
	if ( MeshRenderData.IsValid ( ) )
	{
		return MeshRenderData->CalcBoundByMeshData ( LocalToWorld , BoundsScale );
	}

	return FBoxSphereBounds ( );
}

const TSharedPtr < FLPPDynamicMeshRenderData >& ULPPDynamicMesh::GetRenderData ( ) const
{
	return MeshRenderData;
}
