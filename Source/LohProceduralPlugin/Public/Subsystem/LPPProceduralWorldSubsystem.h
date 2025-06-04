﻿// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Util/ProgressCancel.h"
#include "LPPProceduralWorldSubsystem.generated.h"

struct FMarchingComputeJob
{
	UE::Tasks::FTask               Task;
	TUniquePtr < FProgressCancel > Progress      = nullptr;
	bool                           bCancelled    = false;
	bool                           bHasCompleted = false;

	FString                                                                                                                  DebugName = "";
	TFunction < void  ( FProgressCancel& Progress , TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc >& GameThreadJob ) > JobWork   = nullptr;
};

/**
 * 
 */
UCLASS ( )
class LOHPROCEDURALPLUGIN_API ULPPProceduralWorldSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY ( )

public:

	virtual void Initialize ( FSubsystemCollectionBase& Collection ) override;

	virtual void Tick ( float DeltaTime ) override;

	virtual void Deinitialize ( ) override;

public:

	virtual TStatId GetStatId ( ) const override;

public:

	TWeakPtr < FMarchingComputeJob > LaunchJob ( const TCHAR* DebugName , const TFunction < void  ( FProgressCancel& Progress , TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc >& GameThreadJob ) >& JobWork );

protected:

	FORCEINLINE UE::Tasks::FTask LaunchJobInternal ( FMarchingComputeJob* JobPtr );

public:

	TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc > LazyGameThreadJobQueue;

	FCriticalSection PendingJobsLock;

	bool bIsShuttingDown = false;

	TArray < TSharedPtr < FMarchingComputeJob > > PendingJobs;

	float PreDeltaTime = 0.001f;
};

struct TAsyncMarchingData
{
	explicit TAsyncMarchingData ( UObject* InOuter ) : Outer ( InOuter )
	{
	}

	~TAsyncMarchingData ( )
	{
		if ( LastPendingJobs.IsValid ( ) )
		{
			TSharedPtr < FMarchingComputeJob > LastJob = LastPendingJobs.Pin ( );
			LastJob->bCancelled                        = true;
			UE::Tasks::Wait ( { LastJob->Task } );
		}
	}

	TWeakPtr < FMarchingComputeJob > LastPendingJobs = nullptr;

	TWeakObjectPtr < UObject > Outer = nullptr;

	FORCEINLINE void CancelJob ( )
	{
		check ( Outer.IsExplicitlyNull() == false );

		ULPPProceduralWorldSubsystem* Subsystem = Outer->GetWorld ( )->GetSubsystem < ULPPProceduralWorldSubsystem > ( );

		if ( LastPendingJobs.IsValid ( ) )
		{
			LastPendingJobs.Pin ( )->bCancelled = true;
		}

		LastPendingJobs = nullptr;
	}

	FORCEINLINE void LaunchJob ( const TCHAR* DebugName , const TFunction < void  ( FProgressCancel& Progress , TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc >& GameThreadJob ) >& JobWork )
	{
		check ( Outer.IsExplicitlyNull() == false );

		ULPPProceduralWorldSubsystem* Subsystem = Outer->GetWorld ( )->GetSubsystem < ULPPProceduralWorldSubsystem > ( );

		if ( LastPendingJobs.IsValid ( ) )
		{
			LastPendingJobs.Pin ( )->bCancelled = true;
		}

		if ( Subsystem->bIsShuttingDown == false )
		{
			LastPendingJobs = Subsystem->LaunchJob ( DebugName , JobWork );
		}
		else
		{
			LastPendingJobs = nullptr;
		}
	}
};
