#include "PropertyBagJsonUtils.h"

#include "Dom/JsonValue.h"
#include "JsonObjectConverter.h"
#include "StructUtils/InstancedStruct.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/SoftObjectPath.h"
#include "UObject/UObjectGlobals.h"

namespace
{
	FString StripEnumNamespace(const FString& FullName)
	{
		int32 ColonIndex = INDEX_NONE;
		if (FullName.FindLastChar(TEXT(':'), ColonIndex))
		{
			return FullName.Mid(ColonIndex + 1);
		}
		return FullName;
	}

	TSharedPtr<FJsonValue> NullJsonValue()
	{
		TSharedPtr<FJsonValue> Null = MakeShared<FJsonValueNull>();
		return Null;
	}
}

bool FAIToolsPropertyBagJson::TryParseScalarType(const FString& TypeStr, EPropertyBagPropertyType& OutType, FString& OutError)
{
	static const TMap<FString, EPropertyBagPropertyType> TypeMap = {
		{ TEXT("bool"), EPropertyBagPropertyType::Bool },
		{ TEXT("byte"), EPropertyBagPropertyType::Byte },
		{ TEXT("int32"), EPropertyBagPropertyType::Int32 },
		{ TEXT("int64"), EPropertyBagPropertyType::Int64 },
		{ TEXT("float"), EPropertyBagPropertyType::Float },
		{ TEXT("double"), EPropertyBagPropertyType::Double },
		{ TEXT("name"), EPropertyBagPropertyType::Name },
		{ TEXT("string"), EPropertyBagPropertyType::String },
		{ TEXT("text"), EPropertyBagPropertyType::Text },
		{ TEXT("enum"), EPropertyBagPropertyType::Enum },
		{ TEXT("struct"), EPropertyBagPropertyType::Struct },
		{ TEXT("object"), EPropertyBagPropertyType::Object },
		{ TEXT("softObject"), EPropertyBagPropertyType::SoftObject },
		{ TEXT("class"), EPropertyBagPropertyType::Class },
		{ TEXT("softClass"), EPropertyBagPropertyType::SoftClass },
	};

	if (const EPropertyBagPropertyType* Found = TypeMap.Find(TypeStr))
	{
		OutType = *Found;
		return true;
	}

	OutError = FString::Printf(TEXT("unsupported type '%s'"), *TypeStr);
	return false;
}

const UScriptStruct* FAIToolsPropertyBagJson::FindSupportedStruct(const FString& StructTypeName)
{
	if (StructTypeName == TEXT("Vector")) return TBaseStructure<FVector>::Get();
	if (StructTypeName == TEXT("Rotator")) return TBaseStructure<FRotator>::Get();
	if (StructTypeName == TEXT("Transform")) return TBaseStructure<FTransform>::Get();
	if (StructTypeName == TEXT("Color")) return TBaseStructure<FColor>::Get();
	if (StructTypeName == TEXT("LinearColor")) return TBaseStructure<FLinearColor>::Get();
	return nullptr;
}

UEnum* FAIToolsPropertyBagJson::CreateOrUpdateEnum(UObject* Outer, FName FieldName, const TArray<FString>& EnumValues, FString& OutError)
{
	if (EnumValues.Num() == 0)
	{
		OutError = TEXT("\"enumValues\" must be a non-empty array of strings");
		return nullptr;
	}

	const FString EnumObjectName = FString::Printf(TEXT("E_%s"), *FieldName.ToString());

	UEnum* Enum = FindObject<UEnum>(Outer, *EnumObjectName);
	if (!Enum)
	{
		Enum = NewObject<UEnum>(Outer, FName(*EnumObjectName), RF_Public);
	}

	TArray<TPair<FName, int64>> Names;
	Names.Reserve(EnumValues.Num());
	for (int32 Index = 0; Index < EnumValues.Num(); ++Index)
	{
		const FString FullName = FString::Printf(TEXT("%s::%s"), *EnumObjectName, *EnumValues[Index]);
		Names.Add(TPair<FName, int64>(FName(*FullName), static_cast<int64>(Index)));
	}

	if (!Enum->SetEnums(Names, UEnum::ECppForm::Namespaced, EEnumFlags::None, /*bAddMaxKeyIfMissing=*/false))
	{
		OutError = FString::Printf(TEXT("failed to build enum values for field '%s'"), *FieldName.ToString());
		return nullptr;
	}

	return Enum;
}

bool FAIToolsPropertyBagJson::ParseFieldDesc(const FName FieldName, const TSharedPtr<FJsonObject>& FieldJson, UObject* Outer, FParsedFieldDesc& OutDesc, FString& OutError)
{
	if (!FieldJson.IsValid())
	{
		OutError = FString::Printf(TEXT("field '%s' must be a JSON object"), *FieldName.ToString());
		return false;
	}

	OutDesc.Name = FieldName;

	FString TypeStr;
	if (!FieldJson->TryGetStringField(TEXT("type"), TypeStr))
	{
		OutError = FString::Printf(TEXT("field '%s' is missing required \"type\""), *FieldName.ToString());
		return false;
	}

	FString ScalarTypeStr = TypeStr;
	OutDesc.bIsArray = (TypeStr == TEXT("array"));
	if (OutDesc.bIsArray && !FieldJson->TryGetStringField(TEXT("elementType"), ScalarTypeStr))
	{
		OutError = FString::Printf(TEXT("field '%s': type \"array\" requires \"elementType\""), *FieldName.ToString());
		return false;
	}

	if (!TryParseScalarType(ScalarTypeStr, OutDesc.ValueType, OutError))
	{
		OutError = FString::Printf(TEXT("field '%s': %s"), *FieldName.ToString(), *OutError);
		return false;
	}

	switch (OutDesc.ValueType)
	{
	case EPropertyBagPropertyType::Struct:
	{
		FString StructTypeName;
		if (!FieldJson->TryGetStringField(TEXT("structType"), StructTypeName))
		{
			OutError = FString::Printf(TEXT("field '%s': type \"struct\" requires \"structType\""), *FieldName.ToString());
			return false;
		}
		const UScriptStruct* Struct = FindSupportedStruct(StructTypeName);
		if (!Struct)
		{
			OutError = FString::Printf(TEXT("field '%s': unsupported structType '%s' (supported: Vector, Rotator, Transform, Color, LinearColor)"), *FieldName.ToString(), *StructTypeName);
			return false;
		}
		OutDesc.ValueTypeObject = Struct;
		break;
	}
	case EPropertyBagPropertyType::Object:
	case EPropertyBagPropertyType::SoftObject:
	case EPropertyBagPropertyType::Class:
	case EPropertyBagPropertyType::SoftClass:
	{
		FString ClassName;
		if (FieldJson->TryGetStringField(TEXT("objectClass"), ClassName) && !ClassName.IsEmpty())
		{
			const UClass* Resolved = FindFirstObjectSafe<UClass>(*ClassName, EFindFirstObjectOptions::None, ELogVerbosity::Warning, TEXT("AITools objectClass"));
			if (!Resolved)
			{
				OutError = FString::Printf(TEXT("field '%s': objectClass '%s' not found"), *FieldName.ToString(), *ClassName);
				return false;
			}
			OutDesc.ValueTypeObject = Resolved;
		}
		break;
	}
	case EPropertyBagPropertyType::Enum:
	{
		const TArray<TSharedPtr<FJsonValue>>* EnumValuesJson = nullptr;
		if (!FieldJson->TryGetArrayField(TEXT("enumValues"), EnumValuesJson))
		{
			OutError = FString::Printf(TEXT("field '%s': type \"enum\" requires \"enumValues\""), *FieldName.ToString());
			return false;
		}
		TArray<FString> EnumValues;
		EnumValues.Reserve(EnumValuesJson->Num());
		for (const TSharedPtr<FJsonValue>& Value : *EnumValuesJson)
		{
			EnumValues.Add(Value->AsString());
		}
		UEnum* Enum = CreateOrUpdateEnum(Outer, FieldName, EnumValues, OutError);
		if (!Enum)
		{
			return false;
		}
		OutDesc.ValueTypeObject = Enum;
		break;
	}
	default:
		break;
	}

	FieldJson->TryGetStringField(TEXT("category"), OutDesc.Category);
	OutDesc.Value = FieldJson->TryGetField(TEXT("value"));
	return true;
}

bool FAIToolsPropertyBagJson::SetScalarOnBag(FInstancedPropertyBag& Bag, FName Name, EPropertyBagPropertyType Type, const UObject* TypeObject, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
{
	EPropertyBagResult Result = EPropertyBagResult::Success;
	switch (Type)
	{
	case EPropertyBagPropertyType::Bool:   Result = Bag.SetValueBool(Name, JsonValue->AsBool()); break;
	case EPropertyBagPropertyType::Byte:   Result = Bag.SetValueByte(Name, static_cast<uint8>(JsonValue->AsNumber())); break;
	case EPropertyBagPropertyType::Int32:  Result = Bag.SetValueInt32(Name, static_cast<int32>(JsonValue->AsNumber())); break;
	case EPropertyBagPropertyType::Int64:  Result = Bag.SetValueInt64(Name, static_cast<int64>(JsonValue->AsNumber())); break;
	case EPropertyBagPropertyType::Float:  Result = Bag.SetValueFloat(Name, static_cast<float>(JsonValue->AsNumber())); break;
	case EPropertyBagPropertyType::Double: Result = Bag.SetValueDouble(Name, JsonValue->AsNumber()); break;
	case EPropertyBagPropertyType::Name:   Result = Bag.SetValueName(Name, FName(*JsonValue->AsString())); break;
	case EPropertyBagPropertyType::String: Result = Bag.SetValueString(Name, JsonValue->AsString()); break;
	case EPropertyBagPropertyType::Text:   Result = Bag.SetValueText(Name, FText::FromString(JsonValue->AsString())); break;
	case EPropertyBagPropertyType::Enum:
	{
		const UEnum* Enum = Cast<UEnum>(TypeObject);
		if (!Enum)
		{
			OutError = TEXT("missing enum definition");
			return false;
		}
		const int64 FoundValue = Enum->GetValueByNameString(JsonValue->AsString());
		if (FoundValue == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("'%s' is not one of the declared enumValues"), *JsonValue->AsString());
			return false;
		}
		Result = Bag.SetValueEnum(Name, static_cast<uint8>(FoundValue), Enum);
		break;
	}
	case EPropertyBagPropertyType::Struct:
	{
		const UScriptStruct* Struct = Cast<UScriptStruct>(TypeObject);
		const TSharedPtr<FJsonObject>* StructObj = nullptr;
		if (!Struct || !JsonValue->TryGetObject(StructObj))
		{
			OutError = TEXT("expected a JSON object for a struct value");
			return false;
		}
		FInstancedStruct TempStruct;
		TempStruct.InitializeAs(Struct);
		FText FailReason;
		if (!FJsonObjectConverter::JsonObjectToUStruct(StructObj->ToSharedRef(), Struct, TempStruct.GetMutableMemory(), 0, 0, false, &FailReason))
		{
			OutError = FailReason.ToString();
			return false;
		}
		Result = Bag.SetValueStruct(Name, FConstStructView(TempStruct));
		break;
	}
	case EPropertyBagPropertyType::Object:
	{
		const FString Path = JsonValue->AsString();
		UClass* ObjectClass = const_cast<UClass*>(Cast<UClass>(TypeObject));
		UObject* Loaded = Path.IsEmpty() ? nullptr : StaticLoadObject(ObjectClass ? ObjectClass : UObject::StaticClass(), nullptr, *Path);
		Result = Bag.SetValueObject(Name, Loaded);
		break;
	}
	case EPropertyBagPropertyType::SoftObject:
	case EPropertyBagPropertyType::SoftClass:
		Result = Bag.SetValueSoftPath(Name, FSoftObjectPath(JsonValue->AsString()));
		break;
	case EPropertyBagPropertyType::Class:
	{
		const FString Path = JsonValue->AsString();
		UClass* Loaded = Path.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *Path);
		Result = Bag.SetValueClass(Name, Loaded);
		break;
	}
	default:
		OutError = TEXT("unsupported type");
		return false;
	}

	if (Result != EPropertyBagResult::Success)
	{
		OutError = FString::Printf(TEXT("failed to set value (code %d)"), static_cast<int32>(Result));
		return false;
	}
	return true;
}

bool FAIToolsPropertyBagJson::SetScalarOnArray(FPropertyBagArrayRef& ArrayRef, int32 Index, EPropertyBagPropertyType Type, const UObject* TypeObject, const TSharedPtr<FJsonValue>& JsonValue, FString& OutError)
{
	EPropertyBagResult Result = EPropertyBagResult::Success;
	switch (Type)
	{
	case EPropertyBagPropertyType::Bool:   Result = ArrayRef.SetValueBool(Index, JsonValue->AsBool()); break;
	case EPropertyBagPropertyType::Byte:   Result = ArrayRef.SetValueByte(Index, static_cast<uint8>(JsonValue->AsNumber())); break;
	case EPropertyBagPropertyType::Int32:  Result = ArrayRef.SetValueInt32(Index, static_cast<int32>(JsonValue->AsNumber())); break;
	case EPropertyBagPropertyType::Int64:  Result = ArrayRef.SetValueInt64(Index, static_cast<int64>(JsonValue->AsNumber())); break;
	case EPropertyBagPropertyType::Float:  Result = ArrayRef.SetValueFloat(Index, static_cast<float>(JsonValue->AsNumber())); break;
	case EPropertyBagPropertyType::Double: Result = ArrayRef.SetValueDouble(Index, JsonValue->AsNumber()); break;
	case EPropertyBagPropertyType::Name:   Result = ArrayRef.SetValueName(Index, FName(*JsonValue->AsString())); break;
	case EPropertyBagPropertyType::String: Result = ArrayRef.SetValueString(Index, JsonValue->AsString()); break;
	case EPropertyBagPropertyType::Text:   Result = ArrayRef.SetValueText(Index, FText::FromString(JsonValue->AsString())); break;
	case EPropertyBagPropertyType::Enum:
	{
		const UEnum* Enum = Cast<UEnum>(TypeObject);
		if (!Enum)
		{
			OutError = TEXT("missing enum definition");
			return false;
		}
		const int64 FoundValue = Enum->GetValueByNameString(JsonValue->AsString());
		if (FoundValue == INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("'%s' is not one of the declared enumValues"), *JsonValue->AsString());
			return false;
		}
		Result = ArrayRef.SetValueEnum(Index, static_cast<uint8>(FoundValue), Enum);
		break;
	}
	case EPropertyBagPropertyType::Struct:
	{
		const UScriptStruct* Struct = Cast<UScriptStruct>(TypeObject);
		const TSharedPtr<FJsonObject>* StructObj = nullptr;
		if (!Struct || !JsonValue->TryGetObject(StructObj))
		{
			OutError = TEXT("expected a JSON object for a struct value");
			return false;
		}
		FInstancedStruct TempStruct;
		TempStruct.InitializeAs(Struct);
		FText FailReason;
		if (!FJsonObjectConverter::JsonObjectToUStruct(StructObj->ToSharedRef(), Struct, TempStruct.GetMutableMemory(), 0, 0, false, &FailReason))
		{
			OutError = FailReason.ToString();
			return false;
		}
		Result = ArrayRef.SetValueStruct(Index, FConstStructView(TempStruct));
		break;
	}
	case EPropertyBagPropertyType::Object:
	{
		const FString Path = JsonValue->AsString();
		UClass* ObjectClass = const_cast<UClass*>(Cast<UClass>(TypeObject));
		UObject* Loaded = Path.IsEmpty() ? nullptr : StaticLoadObject(ObjectClass ? ObjectClass : UObject::StaticClass(), nullptr, *Path);
		Result = ArrayRef.SetValueObject(Index, Loaded);
		break;
	}
	case EPropertyBagPropertyType::SoftObject:
	case EPropertyBagPropertyType::SoftClass:
		Result = ArrayRef.SetValueSoftPath(Index, FSoftObjectPath(JsonValue->AsString()));
		break;
	case EPropertyBagPropertyType::Class:
	{
		const FString Path = JsonValue->AsString();
		UClass* Loaded = Path.IsEmpty() ? nullptr : LoadObject<UClass>(nullptr, *Path);
		Result = ArrayRef.SetValueClass(Index, Loaded);
		break;
	}
	default:
		OutError = TEXT("unsupported type");
		return false;
	}

	if (Result != EPropertyBagResult::Success)
	{
		OutError = FString::Printf(TEXT("failed to set array element %d (code %d)"), Index, static_cast<int32>(Result));
		return false;
	}
	return true;
}

TSharedPtr<FJsonValue> FAIToolsPropertyBagJson::GetScalarFromBag(const FInstancedPropertyBag& Bag, const FPropertyBagPropertyDesc& Desc, EPropertyBagPropertyType Type, const UObject* TypeObject)
{
	switch (Type)
	{
	case EPropertyBagPropertyType::Bool:
	{
		auto Result = Bag.GetValueBool(Desc);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueBoolean>(Result.GetValue());
	}
	case EPropertyBagPropertyType::Byte:
	{
		auto Result = Bag.GetValueByte(Desc);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueNumber>(Result.GetValue());
	}
	case EPropertyBagPropertyType::Int32:
	{
		auto Result = Bag.GetValueInt32(Desc);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueNumber>(Result.GetValue());
	}
	case EPropertyBagPropertyType::Int64:
	{
		auto Result = Bag.GetValueInt64(Desc);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueNumber>(static_cast<double>(Result.GetValue()));
	}
	case EPropertyBagPropertyType::Float:
	{
		auto Result = Bag.GetValueFloat(Desc);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueNumber>(Result.GetValue());
	}
	case EPropertyBagPropertyType::Double:
	{
		auto Result = Bag.GetValueDouble(Desc);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueNumber>(Result.GetValue());
	}
	case EPropertyBagPropertyType::Name:
	{
		auto Result = Bag.GetValueName(Desc);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueString>(Result.GetValue().ToString());
	}
	case EPropertyBagPropertyType::String:
	{
		auto Result = Bag.GetValueString(Desc);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueString>(Result.GetValue());
	}
	case EPropertyBagPropertyType::Text:
	{
		auto Result = Bag.GetValueText(Desc);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueString>(Result.GetValue().ToString());
	}
	case EPropertyBagPropertyType::Enum:
	{
		const UEnum* Enum = Cast<UEnum>(TypeObject);
		if (!Enum) return TSharedPtr<FJsonValue>();
		auto Result = Bag.GetValueEnum(Desc, Enum);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueString>(StripEnumNamespace(Enum->GetNameStringByValue(Result.GetValue())));
	}
	case EPropertyBagPropertyType::Struct:
	{
		auto Result = Bag.GetValueStruct(Desc);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		const FStructView View = Result.GetValue();
		TSharedRef<FJsonObject> StructJson = MakeShared<FJsonObject>();
		FJsonObjectConverter::UStructToJsonObject(View.GetScriptStruct(), View.GetMemory(), StructJson);
		return MakeShared<FJsonValueObject>(StructJson);
	}
	case EPropertyBagPropertyType::Object:
	{
		auto Result = Bag.GetValueObject(Desc);
		if (!Result.IsValid() || Result.GetValue() == nullptr) return NullJsonValue();
		return MakeShared<FJsonValueString>(Result.GetValue()->GetPathName());
	}
	case EPropertyBagPropertyType::SoftObject:
	case EPropertyBagPropertyType::SoftClass:
	{
		auto Result = Bag.GetValueSoftPath(Desc);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueString>(Result.GetValue().ToString());
	}
	case EPropertyBagPropertyType::Class:
	{
		auto Result = Bag.GetValueClass(Desc);
		if (!Result.IsValid() || Result.GetValue() == nullptr) return NullJsonValue();
		return MakeShared<FJsonValueString>(Result.GetValue()->GetPathName());
	}
	default:
		return TSharedPtr<FJsonValue>();
	}
}

TSharedPtr<FJsonValue> FAIToolsPropertyBagJson::GetScalarFromArray(const FPropertyBagArrayRef& ArrayRef, int32 Index, EPropertyBagPropertyType Type, const UObject* TypeObject)
{
	switch (Type)
	{
	case EPropertyBagPropertyType::Bool:
	{
		auto Result = ArrayRef.GetValueBool(Index);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueBoolean>(Result.GetValue());
	}
	case EPropertyBagPropertyType::Byte:
	{
		auto Result = ArrayRef.GetValueByte(Index);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueNumber>(Result.GetValue());
	}
	case EPropertyBagPropertyType::Int32:
	{
		auto Result = ArrayRef.GetValueInt32(Index);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueNumber>(Result.GetValue());
	}
	case EPropertyBagPropertyType::Int64:
	{
		auto Result = ArrayRef.GetValueInt64(Index);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueNumber>(static_cast<double>(Result.GetValue()));
	}
	case EPropertyBagPropertyType::Float:
	{
		auto Result = ArrayRef.GetValueFloat(Index);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueNumber>(Result.GetValue());
	}
	case EPropertyBagPropertyType::Double:
	{
		auto Result = ArrayRef.GetValueDouble(Index);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueNumber>(Result.GetValue());
	}
	case EPropertyBagPropertyType::Name:
	{
		auto Result = ArrayRef.GetValueName(Index);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueString>(Result.GetValue().ToString());
	}
	case EPropertyBagPropertyType::String:
	{
		auto Result = ArrayRef.GetValueString(Index);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueString>(Result.GetValue());
	}
	case EPropertyBagPropertyType::Text:
	{
		auto Result = ArrayRef.GetValueText(Index);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueString>(Result.GetValue().ToString());
	}
	case EPropertyBagPropertyType::Enum:
	{
		const UEnum* Enum = Cast<UEnum>(TypeObject);
		if (!Enum) return TSharedPtr<FJsonValue>();
		auto Result = ArrayRef.GetValueEnum(Index, Enum);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueString>(StripEnumNamespace(Enum->GetNameStringByValue(Result.GetValue())));
	}
	case EPropertyBagPropertyType::Struct:
	{
		auto Result = ArrayRef.GetValueStruct(Index);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		const FStructView View = Result.GetValue();
		TSharedRef<FJsonObject> StructJson = MakeShared<FJsonObject>();
		FJsonObjectConverter::UStructToJsonObject(View.GetScriptStruct(), View.GetMemory(), StructJson);
		return MakeShared<FJsonValueObject>(StructJson);
	}
	case EPropertyBagPropertyType::Object:
	{
		auto Result = ArrayRef.GetValueObject(Index);
		if (!Result.IsValid() || Result.GetValue() == nullptr) return NullJsonValue();
		return MakeShared<FJsonValueString>(Result.GetValue()->GetPathName());
	}
	case EPropertyBagPropertyType::SoftObject:
	case EPropertyBagPropertyType::SoftClass:
	{
		auto Result = ArrayRef.GetValueSoftPath(Index);
		if (!Result.IsValid()) return TSharedPtr<FJsonValue>();
		return MakeShared<FJsonValueString>(Result.GetValue().ToString());
	}
	case EPropertyBagPropertyType::Class:
	{
		auto Result = ArrayRef.GetValueClass(Index);
		if (!Result.IsValid() || Result.GetValue() == nullptr) return NullJsonValue();
		return MakeShared<FJsonValueString>(Result.GetValue()->GetPathName());
	}
	default:
		return TSharedPtr<FJsonValue>();
	}
}

FString FAIToolsPropertyBagJson::TypeToString(EPropertyBagPropertyType Type)
{
	switch (Type)
	{
	case EPropertyBagPropertyType::Bool: return TEXT("bool");
	case EPropertyBagPropertyType::Byte: return TEXT("byte");
	case EPropertyBagPropertyType::Int32: return TEXT("int32");
	case EPropertyBagPropertyType::Int64: return TEXT("int64");
	case EPropertyBagPropertyType::Float: return TEXT("float");
	case EPropertyBagPropertyType::Double: return TEXT("double");
	case EPropertyBagPropertyType::Name: return TEXT("name");
	case EPropertyBagPropertyType::String: return TEXT("string");
	case EPropertyBagPropertyType::Text: return TEXT("text");
	case EPropertyBagPropertyType::Enum: return TEXT("enum");
	case EPropertyBagPropertyType::Struct: return TEXT("struct");
	case EPropertyBagPropertyType::Object: return TEXT("object");
	case EPropertyBagPropertyType::SoftObject: return TEXT("softObject");
	case EPropertyBagPropertyType::Class: return TEXT("class");
	case EPropertyBagPropertyType::SoftClass: return TEXT("softClass");
	default: return TEXT("unknown");
	}
}

bool FAIToolsPropertyBagJson::SyncFieldsFromJson(FInstancedPropertyBag& Bag, const TSharedRef<FJsonObject>& FieldsJson, UObject* Outer, FString& OutError)
{
	TArray<FParsedFieldDesc> ParsedFields;
	ParsedFields.Reserve(FieldsJson->Values.Num());
	for (const auto& Pair : FieldsJson->Values)
	{
		const TSharedPtr<FJsonObject>* FieldObj = nullptr;
		if (!Pair.Value->TryGetObject(FieldObj))
		{
			OutError = FString::Printf(TEXT("field '%s' must be a JSON object"), *Pair.Key);
			return false;
		}

		FParsedFieldDesc Desc;
		if (!ParseFieldDesc(FName(*Pair.Key), *FieldObj, Outer, Desc, OutError))
		{
			return false;
		}
		ParsedFields.Add(MoveTemp(Desc));
	}

	TArray<FName> NamesToKeep;
	NamesToKeep.Reserve(ParsedFields.Num());
	for (const FParsedFieldDesc& Desc : ParsedFields)
	{
		NamesToKeep.Add(Desc.Name);
	}

	TArray<FName> NamesToRemove;
	if (const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct())
	{
		for (const FPropertyBagPropertyDesc& ExistingDesc : BagStruct->GetPropertyDescs())
		{
			if (!NamesToKeep.Contains(ExistingDesc.Name))
			{
				NamesToRemove.Add(ExistingDesc.Name);
			}
		}
	}
	if (NamesToRemove.Num() > 0)
	{
		Bag.RemovePropertiesByName(NamesToRemove);
	}

	TArray<FPropertyBagPropertyDesc> Descs;
	Descs.Reserve(ParsedFields.Num());
	for (const FParsedFieldDesc& Field : ParsedFields)
	{
		if (Field.bIsArray)
		{
			Descs.Add(FPropertyBagPropertyDesc(Field.Name, EPropertyBagContainerType::Array, Field.ValueType, Field.ValueTypeObject));
		}
		else
		{
			Descs.Add(FPropertyBagPropertyDesc(Field.Name, Field.ValueType, Field.ValueTypeObject));
		}

#if WITH_EDITOR
		if (!Field.Category.IsEmpty())
		{
			Descs.Last().SetMetaData(TEXT("Category"), Field.Category);
		}
#endif
	}

	const EPropertyBagAlterationResult AlterationResult = Bag.AddProperties(Descs, /*bOverwrite=*/true);
	if (AlterationResult != EPropertyBagAlterationResult::Success)
	{
		OutError = FString::Printf(TEXT("failed to reconcile property bag layout (code %d) -- field names must be non-empty and contain only valid identifier characters"), static_cast<int32>(AlterationResult));
		return false;
	}

	for (const FParsedFieldDesc& Field : ParsedFields)
	{
		if (!Field.Value.IsValid() || Field.Value->IsNull())
		{
			continue;
		}

		if (Field.bIsArray)
		{
			auto ArrayResult = Bag.GetMutableArrayRef(Field.Name);
			if (!ArrayResult.IsValid())
			{
				OutError = FString::Printf(TEXT("field '%s': could not access array storage"), *Field.Name.ToString());
				return false;
			}
			FPropertyBagArrayRef ArrayRef = ArrayResult.GetValue();
			ArrayRef.EmptyValues();

			const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
			if (!Field.Value->TryGetArray(Elements))
			{
				OutError = FString::Printf(TEXT("field '%s': \"value\" must be a JSON array"), *Field.Name.ToString());
				return false;
			}
			for (const TSharedPtr<FJsonValue>& Element : *Elements)
			{
				const int32 NewIndex = ArrayRef.AddValue();
				if (!SetScalarOnArray(ArrayRef, NewIndex, Field.ValueType, Field.ValueTypeObject, Element, OutError))
				{
					OutError = FString::Printf(TEXT("field '%s': %s"), *Field.Name.ToString(), *OutError);
					return false;
				}
			}
		}
		else if (!SetScalarOnBag(Bag, Field.Name, Field.ValueType, Field.ValueTypeObject, Field.Value, OutError))
		{
			OutError = FString::Printf(TEXT("field '%s': %s"), *Field.Name.ToString(), *OutError);
			return false;
		}
	}

	return true;
}

TSharedRef<FJsonObject> FAIToolsPropertyBagJson::FieldsToJson(const FInstancedPropertyBag& Bag)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	const UPropertyBag* BagStruct = Bag.GetPropertyBagStruct();
	if (!BagStruct)
	{
		return Result;
	}

	for (const FPropertyBagPropertyDesc& Desc : BagStruct->GetPropertyDescs())
	{
		TSharedRef<FJsonObject> FieldJson = MakeShared<FJsonObject>();
		const bool bIsArray = Desc.ContainerTypes.GetFirstContainerType() == EPropertyBagContainerType::Array;

		FieldJson->SetStringField(TEXT("type"), bIsArray ? TEXT("array") : TypeToString(Desc.ValueType));
		if (bIsArray)
		{
			FieldJson->SetStringField(TEXT("elementType"), TypeToString(Desc.ValueType));
		}

		if (Desc.ValueType == EPropertyBagPropertyType::Struct)
		{
			if (const UScriptStruct* Struct = Cast<UScriptStruct>(Desc.ValueTypeObject))
			{
				FieldJson->SetStringField(TEXT("structType"), Struct->GetName());
			}
		}
		else if (Desc.ValueType == EPropertyBagPropertyType::Object || Desc.ValueType == EPropertyBagPropertyType::SoftObject
			|| Desc.ValueType == EPropertyBagPropertyType::Class || Desc.ValueType == EPropertyBagPropertyType::SoftClass)
		{
			if (const UClass* Class = Cast<UClass>(Desc.ValueTypeObject))
			{
				FieldJson->SetStringField(TEXT("objectClass"), Class->GetName());
			}
		}
		else if (Desc.ValueType == EPropertyBagPropertyType::Enum)
		{
			if (const UEnum* Enum = Cast<UEnum>(Desc.ValueTypeObject))
			{
				TArray<TSharedPtr<FJsonValue>> EnumValues;
				for (int32 Index = 0; Index < Enum->NumEnums(); ++Index)
				{
					EnumValues.Add(MakeShared<FJsonValueString>(Enum->GetAuthoredNameStringByIndex(Index)));
				}
				FieldJson->SetArrayField(TEXT("enumValues"), EnumValues);
			}
		}

#if WITH_EDITOR
		if (Desc.HasMetaData(TEXT("Category")))
		{
			FieldJson->SetStringField(TEXT("category"), Desc.GetMetaData(TEXT("Category")));
		}
#endif

		TSharedPtr<FJsonValue> ValueJson;
		if (bIsArray)
		{
			auto ArrayResult = Bag.GetArrayRef(Desc);
			if (ArrayResult.IsValid())
			{
				const FPropertyBagArrayRef& ArrayRef = ArrayResult.GetValue();
				TArray<TSharedPtr<FJsonValue>> Elements;
				for (int32 Index = 0; Index < ArrayRef.Num(); ++Index)
				{
					Elements.Add(GetScalarFromArray(ArrayRef, Index, Desc.ValueType, Desc.ValueTypeObject));
				}
				ValueJson = MakeShared<FJsonValueArray>(Elements);
			}
		}
		else
		{
			ValueJson = GetScalarFromBag(Bag, Desc, Desc.ValueType, Desc.ValueTypeObject);
		}

		if (!ValueJson.IsValid())
		{
			ValueJson = NullJsonValue();
		}
		FieldJson->SetField(TEXT("value"), ValueJson);

		Result->SetObjectField(Desc.Name.ToString(), FieldJson);
	}

	return Result;
}

bool FAIToolsPropertyBagJson::ValidateFieldsJson(const TSharedRef<FJsonObject>& FieldsJson, FString& OutError)
{
	UObject* ValidationOuter = GetTransientPackage();
	for (const auto& Pair : FieldsJson->Values)
	{
		const TSharedPtr<FJsonObject>* FieldObj = nullptr;
		if (!Pair.Value->TryGetObject(FieldObj))
		{
			OutError = FString::Printf(TEXT("field '%s' must be a JSON object"), *Pair.Key);
			return false;
		}

		FParsedFieldDesc Desc;
		if (!ParseFieldDesc(FName(*Pair.Key), *FieldObj, ValidationOuter, Desc, OutError))
		{
			return false;
		}
	}
	return true;
}

TSharedRef<FJsonObject> FAIToolsPropertyBagJson::GetSupportedTypesJson()
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> Types;
	for (const TCHAR* T : { TEXT("bool"), TEXT("byte"), TEXT("int32"), TEXT("int64"), TEXT("float"), TEXT("double"),
		TEXT("name"), TEXT("string"), TEXT("text"), TEXT("enum"), TEXT("struct"), TEXT("object"),
		TEXT("softObject"), TEXT("class"), TEXT("softClass") })
	{
		Types.Add(MakeShared<FJsonValueString>(T));
	}
	Result->SetArrayField(TEXT("types"), Types);
	Result->SetStringField(TEXT("containerType"), TEXT("array"));

	TArray<TSharedPtr<FJsonValue>> Structs;
	for (const TCHAR* S : { TEXT("Vector"), TEXT("Rotator"), TEXT("Transform"), TEXT("Color"), TEXT("LinearColor") })
	{
		Structs.Add(MakeShared<FJsonValueString>(S));
	}
	Result->SetArrayField(TEXT("supportedStructTypes"), Structs);

	return Result;
}
