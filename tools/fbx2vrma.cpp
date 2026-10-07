// FBX(Mixamoなど)のアニメーション → VRMA(VRMのアニメーション)の変換ツール。ウィンドウは作らない。
// 使い方: fbx2vrma <入力.fbx> <出力.vrma>(パスはリポジトリ直下からの相対パス)
#include "model/FbxLoader.h"
#include "model/Vrma.h"
#include "resources/ResourcePaths.h"
#include <SDL3/SDL_log.h>

int main(int argc, char *argv[])
{
	if(argc != 3){
		SDL_Log("usage: fbx2vrma <in.fbx> <out.vrma>");
		return 2;
	}
	const auto animation = model::loadFbxAnimation(ResourcePaths::resource(argv[1]));
	if(!animation || !model::saveVrma(ResourcePaths::resource(argv[2]), *animation)){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "fbx2vrma: conversion failed.");
		return 1;
	}
	SDL_Log("fbx2vrma: wrote %s", argv[2]);
	return 0;
}
