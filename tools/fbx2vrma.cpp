// FBX(Mixamoなど)のアニメーション → VRMA(VRMのアニメーション)の変換ツール。ウィンドウは作らない。
// 使い方: fbx2vrma <入力.fbx> <出力.vrma>(パスはそのまま使う。カレントディレクトリからの相対パスでも絶対パスでもよい。先頭の~はホームに展開する)
#include "model/FbxLoader.h"
#include "model/Vrma.h"
#include <SDL3/SDL_log.h>
#include <cstdlib>
#include <string>

// クォートされて、シェルが展開しなかった先頭の~を、ホームディレクトリに直す
static std::string expandPath(const char *path)
{
	const char *home = std::getenv("HOME");
	if(home && path[0] == '~' && (path[1] == '/' || path[1] == '\0')){
		return std::string(home) + (path + 1);
	}
	return path;
}

int main(int argc, char *argv[])
{
	if(argc != 3){
		SDL_Log("usage: fbx2vrma <in.fbx> <out.vrma>");
		return 2;
	}
	const auto animation = model::loadFbxAnimation(expandPath(argv[1]));
	if(!animation || !model::saveVrma(expandPath(argv[2]), *animation)){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "fbx2vrma: conversion failed.");
		return 1;
	}
	SDL_Log("fbx2vrma: wrote %s", argv[2]);
	return 0;
}
