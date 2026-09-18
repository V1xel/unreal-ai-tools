#include "AIToolsHttpServer.h"

#include "AIToolsModule.h"
#include "AssetOperations.h"
#include "HttpPath.h"
#include "HttpServerModule.h"
#include "HttpServerRequest.h"
#include "HttpServerResponse.h"
#include "IHttpRouter.h"
#include "JsonDataAssetOperations.h"
#include "ObserverOperations.h"
#include "SceneObjectOperations.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "TransformOperations.h"

void FAIToolsHttpServer::Start(uint32 Port)
{
	Router = FHttpServerModule::Get().GetHttpRouter(Port, /*bFailOnBindFailure=*/true);
	if (!Router)
	{
		UE_LOG(LogAITools, Error, TEXT("Failed to bind AI Tools HTTP server to port %u -- is it already in use?"), Port);
		return;
	}

	RegisterRoutes();
	FHttpServerModule::Get().StartAllListeners();
	UE_LOG(LogAITools, Log, TEXT("AI Tools HTTP server listening on http://localhost:%u"), Port);
}

void FAIToolsHttpServer::Stop()
{
	if (Router)
	{
		for (const FHttpRouteHandle& Handle : RouteHandles)
		{
			Router->UnbindRoute(Handle);
		}
		RouteHandles.Empty();
		Router.Reset();
	}
}

void FAIToolsHttpServer::RegisterRoutes()
{
	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/health")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleHealth)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/data-asset/types")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleTypes)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/data-asset/sync")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleSync)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/data-asset/read")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleRead)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/data-asset/list")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleList)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/data-asset/validate")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleValidate)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/data-asset/referencers")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleReferencers)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/asset/move")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleMoveAsset)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/asset/delete")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleDeleteAsset)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/asset/list")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleListAssets)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/observer/place")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandlePlaceObserver)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/observer/list")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleListObservers)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/observer/delete")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleDeleteObserver)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/observer/describe")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleDescribeObserver)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/object/create")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleCreateObject)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/object/remove")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleRemoveObject)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/object/list")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleListObjects)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/object/transform")), EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleSetTransform)));

	RouteHandles.Add(Router->BindRoute(FHttpPath(TEXT("/object/get")), EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FAIToolsHttpServer::HandleGetTransform)));
}

namespace
{
	FVector ParseVectorField(const TSharedPtr<FJsonObject>& Obj, const FVector& Default)
	{
		if (!Obj.IsValid())
		{
			return Default;
		}
		FVector Result = Default;
		Obj->TryGetNumberField(TEXT("x"), Result.X);
		Obj->TryGetNumberField(TEXT("y"), Result.Y);
		Obj->TryGetNumberField(TEXT("z"), Result.Z);
		return Result;
	}

	FRotator ParseRotatorField(const TSharedPtr<FJsonObject>& Obj, const FRotator& Default)
	{
		if (!Obj.IsValid())
		{
			return Default;
		}
		FRotator Result = Default;
		Obj->TryGetNumberField(TEXT("pitch"), Result.Pitch);
		Obj->TryGetNumberField(TEXT("yaw"), Result.Yaw);
		Obj->TryGetNumberField(TEXT("roll"), Result.Roll);
		return Result;
	}
}

bool FAIToolsHttpServer::HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("ok"), true);
	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleTypes(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	OnComplete(MakeJsonResponse(FAIToolsJsonDataAssetOps::ListSupportedTypes()));
	return true;
}

bool FAIToolsHttpServer::HandleSync(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body;
	FString ParseError;
	if (!ParseRequestBodyAsJson(Request, Body, ParseError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, ParseError));
		return true;
	}

	FString AssetPath;
	const TSharedPtr<FJsonObject>* JsonObjPtr = nullptr;
	if (!Body->TryGetStringField(TEXT("path"), AssetPath) || !Body->TryGetObjectField(TEXT("json"), JsonObjPtr) || !JsonObjPtr->IsValid())
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("body must be {\"path\": \"/Game/...\", \"json\": {\"assetType\": \"...\", \"fields\": {...}}}")));
		return true;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsJsonDataAssetOps::SyncJsonDataAsset(AssetPath, JsonObjPtr->ToSharedRef(), Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleRead(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString* PathParam = Request.QueryParams.Find(TEXT("path"));
	if (!PathParam)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing required query parameter 'path'")));
		return true;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsJsonDataAssetOps::ReadDataAssetAsJson(*PathParam, Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleList(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString Folder;
	FString AssetType;
	if (const FString* FolderParam = Request.QueryParams.Find(TEXT("folder")))
	{
		Folder = *FolderParam;
	}
	if (const FString* TypeParam = Request.QueryParams.Find(TEXT("type")))
	{
		AssetType = *TypeParam;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsJsonDataAssetOps::ListDataAssets(Folder, AssetType, Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleValidate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body;
	FString ParseError;
	if (!ParseRequestBodyAsJson(Request, Body, ParseError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, ParseError));
		return true;
	}

	const TSharedPtr<FJsonObject>* JsonObjPtr = nullptr;
	if (!Body->TryGetObjectField(TEXT("json"), JsonObjPtr) || !JsonObjPtr->IsValid())
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("body must be {\"json\": {\"assetType\": \"...\", \"fields\": {...}}}")));
		return true;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsJsonDataAssetOps::ValidateJsonSchema(JsonObjPtr->ToSharedRef(), Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleReferencers(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString* PathParam = Request.QueryParams.Find(TEXT("path"));
	if (!PathParam)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing required query parameter 'path'")));
		return true;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsJsonDataAssetOps::FindReferencingDataAssets(*PathParam, Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleMoveAsset(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body;
	FString ParseError;
	if (!ParseRequestBodyAsJson(Request, Body, ParseError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, ParseError));
		return true;
	}

	FString FromPath, ToPath;
	if (!Body->TryGetStringField(TEXT("from"), FromPath) || !Body->TryGetStringField(TEXT("to"), ToPath))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("body must be {\"from\": \"/Game/...\", \"to\": \"/Game/...\"}")));
		return true;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsAssetOps::MoveAsset(FromPath, ToPath, Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleDeleteAsset(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body;
	FString ParseError;
	if (!ParseRequestBodyAsJson(Request, Body, ParseError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, ParseError));
		return true;
	}

	FString AssetPath;
	if (!Body->TryGetStringField(TEXT("path"), AssetPath))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("body must be {\"path\": \"/Game/...\", \"force\": false}")));
		return true;
	}
	bool bForce = false;
	Body->TryGetBoolField(TEXT("force"), bForce);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsAssetOps::DeleteAsset(AssetPath, bForce, Result, OpError))
	{
		// Result may already carry "referencedBy" (see FAIToolsAssetOps::DeleteAsset) --
		// keep it in the body instead of collapsing to a plain error response.
		Result->SetStringField(TEXT("error"), OpError);
		OnComplete(MakeJsonResponse(Result, EHttpServerResponseCodes::Conflict));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleListAssets(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString Folder;
	FString ClassName;
	if (const FString* FolderParam = Request.QueryParams.Find(TEXT("folder")))
	{
		Folder = *FolderParam;
	}
	if (const FString* ClassParam = Request.QueryParams.Find(TEXT("class")))
	{
		ClassName = *ClassParam;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsAssetOps::ListAssets(Folder, ClassName, Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandlePlaceObserver(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body;
	FString ParseError;
	if (!ParseRequestBodyAsJson(Request, Body, ParseError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, ParseError));
		return true;
	}

	FString Name;
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (!Body->TryGetStringField(TEXT("name"), Name) || !Body->TryGetObjectField(TEXT("location"), LocationObj) || !LocationObj->IsValid())
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest,
			TEXT("body must be {\"name\": \"...\", \"location\": {\"x\":0,\"y\":0,\"z\":0}, \"rotation\": {\"pitch\":0,\"yaw\":0,\"roll\":0}, \"fov\": 90}")));
		return true;
	}

	FVector Location(0.0);
	(*LocationObj)->TryGetNumberField(TEXT("x"), Location.X);
	(*LocationObj)->TryGetNumberField(TEXT("y"), Location.Y);
	(*LocationObj)->TryGetNumberField(TEXT("z"), Location.Z);

	FRotator Rotation(0.0, 0.0, 0.0);
	const TSharedPtr<FJsonObject>* RotationObj = nullptr;
	if (Body->TryGetObjectField(TEXT("rotation"), RotationObj) && RotationObj->IsValid())
	{
		(*RotationObj)->TryGetNumberField(TEXT("pitch"), Rotation.Pitch);
		(*RotationObj)->TryGetNumberField(TEXT("yaw"), Rotation.Yaw);
		(*RotationObj)->TryGetNumberField(TEXT("roll"), Rotation.Roll);
	}

	double FieldOfView = 90.0;
	Body->TryGetNumberField(TEXT("fov"), FieldOfView);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsObserverOps::PlaceObserver(Name, Location, Rotation, static_cast<float>(FieldOfView), Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleListObservers(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsObserverOps::ListObservers(Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleDeleteObserver(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body;
	FString ParseError;
	if (!ParseRequestBodyAsJson(Request, Body, ParseError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, ParseError));
		return true;
	}

	FString Name;
	if (!Body->TryGetStringField(TEXT("name"), Name))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("body must be {\"name\": \"...\"}")));
		return true;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsObserverOps::DeleteObserver(Name, Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleDescribeObserver(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString* NameParam = Request.QueryParams.Find(TEXT("name"));
	if (!NameParam)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing required query parameter 'name'")));
		return true;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsObserverOps::DescribeObserver(*NameParam, Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleCreateObject(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body;
	FString ParseError;
	if (!ParseRequestBodyAsJson(Request, Body, ParseError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, ParseError));
		return true;
	}

	FString Name;
	if (!Body->TryGetStringField(TEXT("name"), Name))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest,
			TEXT("body must be {\"name\": \"...\", \"mesh\": \"...\", \"location\": {...}, \"rotation\": {...}, \"scale\": {...}}")));
		return true;
	}

	FString MeshPath;
	Body->TryGetStringField(TEXT("mesh"), MeshPath);

	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	Body->TryGetObjectField(TEXT("location"), LocationObj);
	const FVector Location = ParseVectorField(LocationObj ? *LocationObj : nullptr, FVector::ZeroVector);

	const TSharedPtr<FJsonObject>* RotationObj = nullptr;
	Body->TryGetObjectField(TEXT("rotation"), RotationObj);
	const FRotator Rotation = ParseRotatorField(RotationObj ? *RotationObj : nullptr, FRotator::ZeroRotator);

	const TSharedPtr<FJsonObject>* ScaleObj = nullptr;
	Body->TryGetObjectField(TEXT("scale"), ScaleObj);
	const FVector Scale = ParseVectorField(ScaleObj ? *ScaleObj : nullptr, FVector::OneVector);

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsSceneObjectOps::CreateObject(Name, MeshPath, Location, Rotation, Scale, Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleRemoveObject(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body;
	FString ParseError;
	if (!ParseRequestBodyAsJson(Request, Body, ParseError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, ParseError));
		return true;
	}

	FString Name;
	if (!Body->TryGetStringField(TEXT("name"), Name))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("body must be {\"name\": \"...\"}")));
		return true;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsSceneObjectOps::RemoveObject(Name, Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleListObjects(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsSceneObjectOps::ListObjects(Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::ServerError, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleSetTransform(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	TSharedPtr<FJsonObject> Body;
	FString ParseError;
	if (!ParseRequestBodyAsJson(Request, Body, ParseError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, ParseError));
		return true;
	}

	FString Name;
	if (!Body->TryGetStringField(TEXT("name"), Name))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest,
			TEXT("body must be {\"name\": \"...\"} plus any of \"location\"/\"rotation\"/\"scale\"")));
		return true;
	}

	TOptional<FVector> Location;
	const TSharedPtr<FJsonObject>* LocationObj = nullptr;
	if (Body->TryGetObjectField(TEXT("location"), LocationObj) && LocationObj->IsValid())
	{
		Location = ParseVectorField(*LocationObj, FVector::ZeroVector);
	}

	TOptional<FRotator> Rotation;
	const TSharedPtr<FJsonObject>* RotationObj = nullptr;
	if (Body->TryGetObjectField(TEXT("rotation"), RotationObj) && RotationObj->IsValid())
	{
		Rotation = ParseRotatorField(*RotationObj, FRotator::ZeroRotator);
	}

	TOptional<FVector> Scale;
	const TSharedPtr<FJsonObject>* ScaleObj = nullptr;
	if (Body->TryGetObjectField(TEXT("scale"), ScaleObj) && ScaleObj->IsValid())
	{
		Scale = ParseVectorField(*ScaleObj, FVector::OneVector);
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsTransformOps::SetTransform(Name, Location, Rotation, Scale, Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::HandleGetTransform(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	const FString* NameParam = Request.QueryParams.Find(TEXT("name"));
	if (!NameParam)
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::BadRequest, TEXT("missing required query parameter 'name'")));
		return true;
	}

	TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
	FString OpError;
	if (!FAIToolsTransformOps::GetTransform(*NameParam, Result, OpError))
	{
		OnComplete(MakeErrorResponse(EHttpServerResponseCodes::NotFound, OpError));
		return true;
	}

	OnComplete(MakeJsonResponse(Result));
	return true;
}

bool FAIToolsHttpServer::ParseRequestBodyAsJson(const FHttpServerRequest& Request, TSharedPtr<FJsonObject>& OutJson, FString& OutError)
{
	if (Request.Body.Num() == 0)
	{
		OutError = TEXT("request body is empty");
		return false;
	}

	auto Conv = StringCast<TCHAR>(reinterpret_cast<const UTF8CHAR*>(Request.Body.GetData()), Request.Body.Num());
	const FString BodyString(Conv.Length(), Conv.Get());

	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
	if (!FJsonSerializer::Deserialize(Reader, OutJson) || !OutJson.IsValid())
	{
		OutError = TEXT("request body is not valid JSON");
		return false;
	}
	return true;
}

TUniquePtr<FHttpServerResponse> FAIToolsHttpServer::MakeJsonResponse(const TSharedRef<FJsonObject>& Json, EHttpServerResponseCodes Code)
{
	FString Output;
	const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Output);
	FJsonSerializer::Serialize(Json, Writer);

	TUniquePtr<FHttpServerResponse> Response = FHttpServerResponse::Create(Output, TEXT("application/json"));
	Response->Code = Code;
	return Response;
}

TUniquePtr<FHttpServerResponse> FAIToolsHttpServer::MakeErrorResponse(EHttpServerResponseCodes Code, const FString& Message)
{
	TSharedRef<FJsonObject> ErrorJson = MakeShared<FJsonObject>();
	ErrorJson->SetStringField(TEXT("error"), Message);
	return MakeJsonResponse(ErrorJson, Code);
}
