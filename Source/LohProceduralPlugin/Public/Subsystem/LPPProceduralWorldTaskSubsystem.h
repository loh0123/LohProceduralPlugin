// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Util/ProgressCancel.h"
#include "LPPProceduralWorldTaskSubsystem.generated.h"

struct FProceduralWorldComputeJob
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
class LOHPROCEDURALPLUGIN_API ULPPProceduralWorldTaskSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY ( )

public:

	virtual void Initialize ( FSubsystemCollectionBase& Collection ) override;

	virtual void Tick ( float DeltaTime ) override;

	virtual void Deinitialize ( ) override;

public:

	virtual TStatId GetStatId ( ) const override;

public:

	TWeakPtr < FProceduralWorldComputeJob > LaunchJob (
		const TCHAR*                                                                                                                    DebugName ,
		const TFunction < void  ( FProgressCancel& Progress , TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc >& GameThreadJob ) >& JobWork ,
		const LowLevelTasks::ETaskPriority                                                                                              Priority          = LowLevelTasks::ETaskPriority::BackgroundHigh ,
		const bool                                                                                                                      bSingleThreadMode = false
		);

protected:

	FORCEINLINE UE::Tasks::FTask LaunchJobInternal ( FProceduralWorldComputeJob* JobPtr , const LowLevelTasks::ETaskPriority Priority = LowLevelTasks::ETaskPriority::BackgroundHigh );

public:

	TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc > LazyGameThreadJobQueue;

	FCriticalSection PendingJobsLock;

	bool bIsShuttingDown = false;

	TArray < TSharedPtr < FProceduralWorldComputeJob > > PendingJobs;

protected:

	UPROPERTY ( Transient )
	float TickBudget = 0.0f;
};

struct TAsyncProceduralWorldTask
{
	explicit TAsyncProceduralWorldTask ( UObject* InOuter ) : Outer ( InOuter )
	{
	}

	~TAsyncProceduralWorldTask ( )
	{
		if ( LastPendingJobs.IsValid ( ) )
		{
			TSharedPtr < FProceduralWorldComputeJob > LastJob = LastPendingJobs.Pin ( );
			LastJob->bCancelled                               = true;
			UE::Tasks::Wait ( { LastJob->Task } );
		}
	}

	TWeakPtr < FProceduralWorldComputeJob > LastPendingJobs = nullptr;

	TWeakObjectPtr < UObject > Outer = nullptr;

	FTimerHandle TaskDelayHandler = FTimerHandle ( );

	FORCEINLINE void CancelJob ( )
	{
		check ( Outer.IsExplicitlyNull() == false );

		ULPPProceduralWorldTaskSubsystem* Subsystem = Outer->GetWorld ( )->GetSubsystem < ULPPProceduralWorldTaskSubsystem > ( );

		if ( LastPendingJobs.IsValid ( ) )
		{
			LastPendingJobs.Pin ( )->bCancelled = true;
		}

		LastPendingJobs = nullptr;
	}

	FORCEINLINE void LaunchJob ( const TCHAR* DebugName , const TFunction < void  ( FProgressCancel& Progress , TQueue < TFunction < void  ( ) > , EQueueMode::Mpsc >& GameThreadJob ) >& JobWork , const LowLevelTasks::ETaskPriority Priority = LowLevelTasks::ETaskPriority::BackgroundHigh , const bool bSingleThreadMode = false )
	{
		check ( Outer.IsExplicitlyNull() == false );

		ULPPProceduralWorldTaskSubsystem* Subsystem = Outer->GetWorld ( )->GetSubsystem < ULPPProceduralWorldTaskSubsystem > ( );

		if ( LastPendingJobs.IsValid ( ) )
		{
			LastPendingJobs.Pin ( )->bCancelled = true;
		}

		if ( Subsystem->bIsShuttingDown == false )
		{
			LastPendingJobs = Subsystem->LaunchJob ( DebugName , JobWork , Priority , bSingleThreadMode );
		}
		else
		{
			LastPendingJobs = nullptr;
		}
	}
};
