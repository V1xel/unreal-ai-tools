#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "HttpResultCallback.h"
#include "HttpRouteHandle.h"
#include "HttpServerConstants.h"

struct FHttpServerRequest;
struct FHttpServerResponse;
class IHttpRouter;

/**
 * Owns the plugin's in-process HTTP server: binds one route per JSON-data-asset operation
 * (see docs/tools/json-data-assets.md in the toolkit repo) to FAIToolsJsonDataAssetOps, and
 * marshals JSON request/response bodies. This is the only place that knows HTTP is involved --
 * everything downstream is plain C++.
 */
class FAIToolsHttpServer
{
public:
	void Start(uint32 Port);
	void Stop();

private:
	TSharedPtr<IHttpRouter> Router;
	TArray<FHttpRouteHandle> RouteHandles;

	void RegisterRoutes();

	bool HandleHealth(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleTypes(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSync(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleRead(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleList(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleValidate(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleReferencers(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleMoveAsset(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleDeleteAsset(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleListAssets(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandlePlaceObserver(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleListObservers(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleDeleteObserver(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleDescribeObserver(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleCreateObject(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleRemoveObject(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleListObjects(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleSetTransform(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleGetTransform(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	static bool ParseRequestBodyAsJson(const FHttpServerRequest& Request, TSharedPtr<FJsonObject>& OutJson, FString& OutError);
	static TUniquePtr<FHttpServerResponse> MakeJsonResponse(const TSharedRef<FJsonObject>& Json, EHttpServerResponseCodes Code = EHttpServerResponseCodes::Ok);
	static TUniquePtr<FHttpServerResponse> MakeErrorResponse(EHttpServerResponseCodes Code, const FString& Message);
};
