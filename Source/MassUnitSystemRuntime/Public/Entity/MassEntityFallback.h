// Copyright Digi Logic Labs LLC. Licensed under the Apache License, Version 2.0.
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "CoreMinimal.h"
#include "MassEntityHandle.h"
#include "MassEntityFallback.generated.h"

/**
 * Stable, Blueprint-friendly representation of a native Mass entity handle.
 * The entity is only valid in the UWorld that created it and must still be
 * validated against that world's FMassEntityManager before use.
 */
USTRUCT(BlueprintType)
struct MASSUNITSYSTEMRUNTIME_API FMassUnitEntityHandle
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	int32 Index = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Mass Unit")
	int32 SerialNumber = 0;

	FMassUnitEntityHandle() = default;
	FMassUnitEntityHandle(const int32 InIndex, const int32 InSerialNumber)
		: Index(InIndex), SerialNumber(InSerialNumber)
	{
	}

	explicit FMassUnitEntityHandle(const FMassEntityHandle InHandle)
		: Index(InHandle.Index), SerialNumber(InHandle.SerialNumber)
	{
	}

	bool IsValid() const { return Index != 0 && SerialNumber != 0; }
	void Invalidate() { Index = 0; SerialNumber = 0; }
	FMassEntityHandle ToMassEntityHandle() const { return FMassEntityHandle(Index, SerialNumber); }
	FString ToString() const { return FString::Printf(TEXT("Entity[%d:%d]"), Index, SerialNumber); }

	friend bool operator==(const FMassUnitEntityHandle& A, const FMassUnitEntityHandle& B)
	{
		return A.Index == B.Index && A.SerialNumber == B.SerialNumber;
	}

	friend bool operator!=(const FMassUnitEntityHandle& A, const FMassUnitEntityHandle& B)
	{
		return !(A == B);
	}
};

FORCEINLINE uint32 GetTypeHash(const FMassUnitEntityHandle& Handle)
{
	return HashCombine(::GetTypeHash(Handle.Index), ::GetTypeHash(Handle.SerialNumber));
}
