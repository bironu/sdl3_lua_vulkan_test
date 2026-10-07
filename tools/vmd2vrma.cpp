// VMD(MMDのモーション) → VRMA(VRMのアニメーション)の変換ツール。ウィンドウは作らない。
// 使い方: vmd2vrma <MMDモデル.pmx> <モーション.vmd> <VRMモデル.vrm> <出力.vrma>
// パスはリポジトリ直下からの相対パス。変換にはMMDのモデルが要るが、できたVRMAは、MMDのモデルが無くても再生できる
#include "model/GltfLoader.h"
#include "model/PmxLoader.h"
#include "model/VmdLoader.h"
#include "model/Vrma.h"
#include "resources/ResourcePaths.h"
#include <SDL3/SDL_log.h>
#include <string>

int main(int argc, char *argv[])
{
	if(argc != 5){
		SDL_Log("usage: vmd2vrma <model.pmx> <motion.vmd> <vrm model.vrm> <out.vrma>");
		return 2;
	}
	const std::string pmxPath = argv[1];
	const auto slash = pmxPath.find_last_of('/');
	const std::string pmxDir = slash == std::string::npos ? std::string() : pmxPath.substr(0, slash);
	const auto mmd = model::loadPmx(ResourcePaths::resource(pmxPath.c_str()), pmxDir);
	const auto motion = model::loadVmd(ResourcePaths::resource(argv[2]));
	const auto vrm = model::loadVrm(ResourcePaths::resource(argv[3]));
	if(!mmd || !motion || !vrm){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "vmd2vrma: failed to load the inputs.");
		return 1;
	}
	if(!model::convertVmdToVrma(*mmd, *motion, *vrm, ResourcePaths::resource(argv[4]))){
		SDL_LogError(SDL_LOG_CATEGORY_ERROR, "vmd2vrma: conversion failed.");
		return 1;
	}
	SDL_Log("vmd2vrma: wrote %s", argv[4]);
	return 0;
}
