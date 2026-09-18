#include "AIToolsModule.h"

#include "AIToolsHttpServer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY(LogAITools);

void FAIToolsModule::StartupModule()
{
	HttpServer = MakeUnique<FAIToolsHttpServer>();
	HttpServer->Start(GetServerPort());
}

void FAIToolsModule::ShutdownModule()
{
	if (HttpServer)
	{
		HttpServer->Stop();
		HttpServer.Reset();
	}
}

FAIToolsModule& FAIToolsModule::Get()
{
	return FModuleManager::LoadModuleChecked<FAIToolsModule>(TEXT("AITools"));
}

bool FAIToolsModule::IsAvailable()
{
	return FModuleManager::Get().IsModuleLoaded(TEXT("AITools"));
}

uint32 FAIToolsModule::GetServerPort()
{
	int32 Port = 8760;
	FParse::Value(FCommandLine::Get(), TEXT("AIToolsPort="), Port);
	return static_cast<uint32>(Port);
}

IMPLEMENT_MODULE(FAIToolsModule, AITools)
