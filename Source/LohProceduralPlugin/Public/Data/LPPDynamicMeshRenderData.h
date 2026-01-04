// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"

/**
 * 
 */
class LOHPROCEDURALPLUGIN_API FLPPDynamicMeshRenderData
{
public:

	FLPPDynamicMeshRenderData ( FLPPDynamicMeshRenderData&& Other ) noexcept : MaterialMapping ( MoveTemp ( Other.MaterialMapping ) ),
	                                                                           MeshData ( MoveTemp ( Other.MeshData ) ),
	                                                                           LocalBounds ( MoveTemp ( Other.LocalBounds ) ),
	                                                                           Bounds ( MoveTemp ( Other.Bounds ) ),
	                                                                           bHasNaniteFallbackMesh ( Other.bHasNaniteFallbackMesh ),
	                                                                           bReadyForStreaming ( Other.bReadyForStreaming ),
	                                                                           bIsInitialized ( Other.bIsInitialized )
	{
		if ( Other.NaniteResourcesPtr.IsValid ( ) )
		{
			NaniteResourcesPtr = MoveTemp ( Other.NaniteResourcesPtr );
		}
		else
		{
			NaniteResourcesPtr = nullptr;
		}

		if ( Other.LumenCardData.IsValid ( ) )
		{
			LumenCardData = MoveTemp ( Other.LumenCardData );
		}
		else
		{
			LumenCardData = nullptr;
		}

		if ( Other.DistanceFieldPtr.IsValid ( ) )
		{
			DistanceFieldPtr = MoveTemp ( Other.DistanceFieldPtr );
		}
		else
		{
			DistanceFieldPtr = nullptr;
		}
	}

	/** Default constructor. */
	FLPPDynamicMeshRenderData ( );

	~FLPPDynamicMeshRenderData ( );

public:

	TArray < uint32 > MaterialMapping;

	UE::Geometry::FDynamicMesh3 MeshData = UE::Geometry::FDynamicMesh3 ( );

	TPimplPtr < Nanite::FResources > NaniteResourcesPtr = nullptr;

	TSharedPtr < FCardRepresentationData > LumenCardData = nullptr;

	TSharedPtr < FDistanceFieldVolumeData > DistanceFieldPtr = nullptr;

	// Local Bounding Box
	UE::Geometry::FAxisAlignedBox3d LocalBounds;

	/** Bounds of the renderable mesh. */
	FBoxSphereBounds Bounds;

#if RHI_RAYTRACING
	//RayTracing::FGeometryGroupHandle RayTracingGeometryGroupHandle = INDEX_NONE;
#endif

private:

	bool bHasNaniteFallbackMesh;

	/** True if rhi resources are initialized */
	bool bReadyForStreaming;

	bool bIsInitialized = false;

public:

	bool IsInitialized ( ) const
	{
		return bIsInitialized;
	}

	bool HasValidNaniteData ( ) const;

	bool HasNaniteFallbackMesh ( EShaderPlatform ShaderPlatform ) const;

public:

	/** Initialize the render resources. */
	void InitResources ( ERHIFeatureLevel::Type InFeatureLevel , UMeshComponent* Owner );

	/** Releases the render resources. */
	void ReleaseResources ( );

	/** Compute the size of this resource. */
	void GetResourceSizeEx ( FResourceSizeEx& CumulativeResourceSize ) const;

	/** Get the estimated memory overhead of buffers marked as NeedsCPUAccess. */
	SIZE_T GetCPUAccessMemoryOverhead ( ) const;

	//void InitializeRayTracingRepresentation ( );

public:

	FORCEINLINE FBoxSphereBounds CalcBoundByMeshData ( const FTransform& LocalToWorld , const float BoundsScale )
	{
		FBox LocalBoundingBox = static_cast < FBox > ( LocalBounds );
		Bounds                = LocalBoundingBox.TransformBy ( LocalToWorld );
		Bounds.BoxExtent      *= BoundsScale;
		Bounds.SphereRadius   *= BoundsScale;

		return Bounds;
	}
};
