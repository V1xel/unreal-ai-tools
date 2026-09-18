#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FAIToolsHttpServer;

DECLARE_LOG_CATEGORY_EXTERN(LogAITools, Log, All);

class FAIToolsModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FAIToolsModule& Get();
	static bool IsAvailable();

	/** Port the in-process HTTP server listens on. Configurable via -AIToolsPort=<port> on the command line. */
	static uint32 GetServerPort();

private:
	TUniquePtr<FAIToolsHttpServer> HttpServer;
};
