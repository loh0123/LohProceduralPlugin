﻿// Copyright (c) 2023-2025 Loh Zhi Kang ( loh0123@hotmail.com )
//
// Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
// or copy at http://opensource.org/licenses/MIT)

#include "Library/LPPMarchingFunctionLibrary.h"

bool ULPPMarchingFunctionLibrary::GetMarchingIDBit( const uint8 MarchingID , const int32 BitIndex )
{
	check(BitIndex >= 0 || BitIndex <= 7);

	return (MarchingID & 1 << (BitIndex & 8 - 1)) != 0;
}

void ULPPMarchingFunctionLibrary::SetMarchingIDBit( uint8& MarchingID , const int32 BitIndex , const bool bNewValue )
{
	check(BitIndex >= 0 || BitIndex <= 7);

	if ( bNewValue )
	{
		MarchingID |= 1 << (BitIndex & 7);
	}
	else
	{
		MarchingID &= ~(1 << (BitIndex & 7));
	}
}

uint8 ULPPMarchingFunctionLibrary::RotateMarchingIDByX( const uint8 MarchingID , const bool bReverse )
{
	constexpr int32 SwapList[6][2] =
		{
			{
				0
				, 2
			}
			, {
				2
				, 6
			}
			, {
				6
				, 4
			}
			, {
				1
				, 3
			}
			, {
				3
				, 7
			}
			, {
				7
				, 5
			}
		};

	uint8 ReturnValue = MarchingID;

	for ( int32 Index = bReverse
		                    ? 5
		                    : 0 ; Index < 6 && Index >= 0 ; bReverse
			                                                    ? Index--
			                                                    : Index++ )
	{
		const bool TempBool = GetMarchingIDBit(ReturnValue, SwapList[Index][0]);

		SetMarchingIDBit(ReturnValue, SwapList[Index][0], GetMarchingIDBit(ReturnValue, SwapList[Index][1]));
		SetMarchingIDBit(ReturnValue, SwapList[Index][1], TempBool);
	}

	return ReturnValue;
}

uint8 ULPPMarchingFunctionLibrary::RotateMarchingIDByY( const uint8 MarchingID , const bool bReverse )
{
	constexpr int32 SwapList[6][2] =
		{
			{
				0
				, 4
			}
			, {
				4
				, 5
			}
			, {
				5
				, 1
			}
			, {
				2
				, 6
			}
			, {
				6
				, 7
			}
			, {
				7
				, 3
			}
		};

	uint8 ReturnValue = MarchingID;

	for ( int32 Index = bReverse
		                    ? 5
		                    : 0 ; Index < 6 && Index >= 0 ; bReverse
			                                                    ? Index--
			                                                    : Index++ )
	{
		const bool TempBool = GetMarchingIDBit(ReturnValue, SwapList[Index][0]);

		SetMarchingIDBit(ReturnValue, SwapList[Index][0], GetMarchingIDBit(ReturnValue, SwapList[Index][1]));
		SetMarchingIDBit(ReturnValue, SwapList[Index][1], TempBool);
	}

	return ReturnValue;
}

uint8 ULPPMarchingFunctionLibrary::RotateMarchingIDByZ( const uint8 MarchingID , const bool bReverse )
{
	constexpr int32 SwapList[6][2] =
		{
			{
				0
				, 2
			}
			, {
				2
				, 3
			}
			, {
				3
				, 1
			}
			, {
				4
				, 6
			}
			, {
				6
				, 7
			}
			, {
				7
				, 5
			}
			,
		};

	uint8 ReturnValue = MarchingID;

	for ( int32 Index = bReverse
		                    ? 5
		                    : 0 ; Index < 6 && Index >= 0 ; bReverse
			                                                    ? Index--
			                                                    : Index++ )
	{
		const bool TempBool = GetMarchingIDBit(ReturnValue, SwapList[Index][0]);

		SetMarchingIDBit(ReturnValue, SwapList[Index][0], GetMarchingIDBit(ReturnValue, SwapList[Index][1]));
		SetMarchingIDBit(ReturnValue, SwapList[Index][1], TempBool);
	}

	return ReturnValue;
}

uint8 ULPPMarchingFunctionLibrary::RotateMarchingID( const uint8 MarchingID , const FIntVector& Rotation )
{
	uint8 ReturnValue = MarchingID;

	const bool bReverseX = Rotation.X < 0;
	const bool bReverseY = Rotation.Y < 0;
	const bool bReverseZ = Rotation.Z < 0;

	FIntVector ABSRotation = FIntVector(FMath::Abs(Rotation.X), FMath::Abs(Rotation.Y), FMath::Abs(Rotation.Z));

	for ( int32 Count = 0 ; Count < ABSRotation.X ; Count++ )
	{
		ReturnValue = RotateMarchingIDByX(ReturnValue, bReverseX);
	}

	for ( int32 Count = 0 ; Count < ABSRotation.Y ; Count++ )
	{
		ReturnValue = RotateMarchingIDByY(ReturnValue, bReverseY);
	}

	for ( int32 Count = 0 ; Count < ABSRotation.Z ; Count++ )
	{
		ReturnValue = RotateMarchingIDByZ(ReturnValue, bReverseZ);
	}

	return ReturnValue;
}

uint8 ULPPMarchingFunctionLibrary::CalculateDualGridMarchingID( const uint8 FullMarchingID , const int32 CalculateIndex )
{
	if ( FullMarchingID == 255 || FullMarchingID == 0 )
	{
		return 0; /* Fill so skip */
	}

	if ( GetMarchingIDBit(FullMarchingID, CalculateIndex) == false )
	{
		return 0; /* CalculateIndex Empty */
	}

	constexpr uint8 DefaultFilterList[8] =
		{
			23
			, 43
			, 77
			, 142
			, 113
			, 178
			, 212
			, 232
		};

	constexpr uint8 CheckMap[8][3] =
		{
			{
				6
				, 18
				, 20
			}
			, {
				9
				, 33
				, 40
			}
			, {
				9
				, 65
				, 72
			}
			, {
				6
				, 130
				, 132
			}
			, {
				96
				, 33
				, 65
			}
			, {
				144
				, 18
				, 130
			}
			, {
				144
				, 20
				, 132
			}
			, {
				96
				, 40
				, 72
			}
			,
		};

	constexpr uint8 ApplyMap[8][3] =
		{
			{
				8
				, 32
				, 64
			}
			, {
				4
				, 16
				, 128
			}
			, {
				2
				, 16
				, 128
			}
			, {
				1
				, 32
				, 64
			}
			, {
				128
				, 2
				, 4
			}
			, {
				64
				, 1
				, 8
			}
			, {
				32
				, 1
				, 8
			}
			, {
				16
				, 2
				, 4
			}
			,
		};

	uint8 CurrentFilter = DefaultFilterList[CalculateIndex];

	for ( int32 Step = 0 ; Step < 3 ; Step++ )
	{
		const uint8 CheckByte = CheckMap[CalculateIndex][Step];
		const uint8 ApplyByte = ApplyMap[CalculateIndex][Step];

		if ( (FullMarchingID & CheckByte) == CheckByte )
		{
			CurrentFilter |= ApplyByte;
		}
	}

	return FullMarchingID & CurrentFilter;
}