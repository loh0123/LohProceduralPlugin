// Fill out your copyright notice in the Description page of Project Settings.


#include "Components/LPPChunkedDynamicMesh.h"

#include "Components/LPPChunkedDynamicMeshProxy.h"
#include "DynamicMesh/MeshTransforms.h"
#include "Math/BoxSphereBounds.h"
#include "PhysicsEngine/PhysicsSettings.h"


// Sets default values for this component's properties
ULPPChunkedDynamicMesh::ULPPChunkedDynamicMesh ( )
{
	// Set this component to be initialized when the game starts, and to be ticked every frame.  You can turn these features
	// off to improve performance if you don't need them.
	PrimaryComponentTick.bCanEverTick = true;

	// ...
}


// Called when the game starts
void ULPPChunkedDynamicMesh::BeginPlay ( )
{
	Super::BeginPlay ( );

	// ...
}


// Called every frame
void ULPPChunkedDynamicMesh::TickComponent ( float DeltaTime , ELevelTick TickType , FActorComponentTickFunction* ThisTickFunction )
{
	Super::TickComponent ( DeltaTime , TickType , ThisTickFunction );

	// ...
}

void ULPPChunkedDynamicMesh::ProcessMesh ( TFunctionRef < void  ( const UE::Geometry::FDynamicMesh3& ) > ProcessFunc ) const
{
	if ( ensure ( MeshObject ) )
	{
		MeshObject->ProcessMesh ( ProcessFunc );
	}
}

void ULPPChunkedDynamicMesh::SetMesh ( UE::Geometry::FDynamicMesh3&& MoveMesh )
{
	if ( ensure ( MeshObject ) )
	{
		MeshObject->SetMesh ( MoveTemp ( MoveMesh ) );

		NotifyMeshUpdated ( );
	}
}

void ULPPChunkedDynamicMesh::ApplyTransform ( const FTransform3d& Transform , bool bInvert )
{
	if ( ensure ( MeshObject ) )
	{
		MeshObject->EditMesh ( [&] ( FDynamicMesh3& EditMesh )
		{
			if ( bInvert )
			{
				MeshTransforms::ApplyTransformInverse ( EditMesh , Transform , true );
			}
			else
			{
				MeshTransforms::ApplyTransform ( EditMesh , Transform , true );
			}
		} , EDynamicMeshChangeType::DeformationEdit , EDynamicMeshAttributeChangeFlags::VertexPositions | EDynamicMeshAttributeChangeFlags::NormalsTangents , /*bDeferChangeEvents*/ false );
	}
}

void ULPPChunkedDynamicMesh::ClearMesh ( )
{
	if ( ensure ( MeshObject ) )
	{
		MeshObject->EditMesh ( [] ( UE::Geometry::FDynamicMesh3& MeshData ) { MeshData.Clear ( ); } );

		InvalidatePhysicsData ( );

		NotifyMeshUpdated ( );
	}
}

void ULPPChunkedDynamicMesh::ApplyChange ( const FMeshVertexChange* Change , bool bRevert )
{
	// will fire UDynamicMesh::MeshChangedEvent, which will call OnMeshObjectChanged() below to invalidate proxy, fire change events, etc
	if ( ensure ( MeshObject ) )
	{
		MeshObject->ApplyChange ( Change , bRevert );
	}
}

void ULPPChunkedDynamicMesh::ApplyChange ( const FMeshChange* Change , bool bRevert )
{
	// will fire UDynamicMesh::MeshChangedEvent, which will call OnMeshObjectChanged() below to invalidate proxy, fire change events, etc
	if ( ensure ( MeshObject ) )
	{
		MeshObject->ApplyChange ( Change , bRevert );
	}
}

void ULPPChunkedDynamicMesh::ApplyChange ( const FMeshReplacementChange* Change , bool bRevert )
{
	// will fire UDynamicMesh::MeshChangedEvent, which will call OnMeshObjectChanged() below to invalidate proxy, fire change events, etc
	if ( ensure ( MeshObject ) )
	{
		MeshObject->ApplyChange ( Change , bRevert );
	}
}

void ULPPChunkedDynamicMesh::NotifyMeshUpdated ( )
{
	RebuildPhysicsData ( );
	UpdateLocalBounds ( );
	MarkRenderStateDirty ( );
}

void ULPPChunkedDynamicMesh::UpdateLocalBounds ( )
{
	LocalBounds = MeshObject ? GetMesh ( )->GetBounds ( true ) : UE::Geometry::FAxisAlignedBox3d::Empty ( );
	if ( LocalBounds.MaxDim ( ) <= 0 )
	{
		// If bbox is empty, set a very small bbox to avoid log spam/etc in other engine systems.
		// The check used is generally IsNearlyZero(), which defaults to KINDA_SMALL_NUMBER, so set 
		// a slightly larger box here to be above that threshold
		LocalBounds = UE::Geometry::FAxisAlignedBox3d ( FVector3d::Zero ( ) , ( double ) ( KINDA_SMALL_NUMBER + SMALL_NUMBER ) );
	}
}

bool ULPPChunkedDynamicMesh::GetTriMeshSizeEstimates ( struct FTriMeshCollisionDataEstimates& OutTriMeshEstimates , bool bInUseAllTriData ) const
{
	ProcessMesh ( [&] ( const FDynamicMesh3& Mesh )
	             {
		             OutTriMeshEstimates.VerticeCount = Mesh.VertexCount ( );
	             }
	            );
	return true;
}

bool ULPPChunkedDynamicMesh::GetPhysicsTriMeshData ( struct FTriMeshCollisionData* CollisionData , bool InUseAllTriData )
{
	ProcessMesh ( [&] ( const FDynamicMesh3& Mesh )
	{
		const FDynamicMeshMaterialAttribute* MaterialAttrib = Mesh.HasAttributes ( ) && Mesh.Attributes ( )->HasMaterialID ( ) ? Mesh.Attributes ( )->GetMaterialID ( ) : nullptr;

		TArray < int32 > VertexMap;
		const bool       bIsSparseV = !Mesh.IsCompactV ( );

		if ( bIsSparseV )
		{
			VertexMap.SetNum ( Mesh.MaxVertexID ( ) );
		}
		CollisionData->Vertices.Reserve ( Mesh.VertexCount ( ) );
		for ( int32 vid : Mesh.VertexIndicesItr ( ) )
		{
			int32 Index = CollisionData->Vertices.Add ( ( FVector3f ) Mesh.GetVertex ( vid ) );
			if ( bIsSparseV )
			{
				VertexMap [ vid ] = Index;
			}
			else
			{
				check ( vid == Index );
			}
		}

		// copy triangles
		CollisionData->Indices.Reserve ( Mesh.TriangleCount ( ) );
		CollisionData->MaterialIndices.Reserve ( Mesh.TriangleCount ( ) );
		for ( int32 tid : Mesh.TriangleIndicesItr ( ) )
		{
			UE::Geometry::FIndex3i Tri = Mesh.GetTriangle ( tid );
			FTriIndices            Triangle;
			if ( bIsSparseV )
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
		CollisionData->bDeformableMesh = false;
		CollisionData->bFastCook       = false;
	} );

	return true;
}

bool ULPPChunkedDynamicMesh::ContainsPhysicsTriMeshData ( bool InUseAllTriData ) const
{
	if ( MeshObject != nullptr )
	{
		if ( int32 TriangleCount = MeshObject->GetTriangleCount ( ) ; TriangleCount > 0 )
		{
			return true;
		}
	}
	return false;
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

		if ( FBaseDynamicMeshSceneProxy* Proxy = GetBaseSceneProxy ( ) )
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

	if ( MeshBodySetup->CurrentCookHelper )
	{
		MeshBodySetup->AbortPhysicsMeshAsyncCreation ( );
	}
	
	if ( GetMesh ( )->TriangleCount ( ) > 0 )
	{
		MeshBodySetup->CreatePhysicsMeshesAsync ( FOnAsyncPhysicsCookFinished::CreateUObject ( this , &ULPPChunkedDynamicMesh::FinishPhysicsAsyncCook , MeshBodySetup.Get ( ) ) );
	}
	else
	{
		MeshBodySetup->RemoveSimpleCollision ( );
		DestroyPhysicsState ( );

		if ( FBaseDynamicMeshSceneProxy* Proxy = GetBaseSceneProxy ( ) )
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

		if ( FBaseDynamicMeshSceneProxy* Proxy = GetBaseSceneProxy ( ) )
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

FBaseDynamicMeshSceneProxy* ULPPChunkedDynamicMesh::GetBaseSceneProxy ( )
{
	return reinterpret_cast < FBaseDynamicMeshSceneProxy* > ( GetSceneProxy ( ) );
}

FPrimitiveSceneProxy* ULPPChunkedDynamicMesh::CreateSceneProxy ( )
{
	// if this is not always the case, we have made incorrect assumptions
	ensure ( GetSceneProxy() == nullptr );

	FLPPChunkedDynamicMeshProxy* NewProxy = nullptr;
	if ( MeshObject && GetMesh ( )->TriangleCount ( ) > 0 )
	{
		NewProxy = new FLPPChunkedDynamicMeshProxy ( this );

		NewProxy->Initialize ( );

		NewProxy->SetVerifyUsedMaterials ( true );
	}

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
