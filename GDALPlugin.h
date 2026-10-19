#include "SubSystems/FocalEngine/SubSystems/PluginSystem/FEPluginConnector.h"
using namespace FocalEngine;

//#include "CGALDeclarations.h"

#pragma warning (disable: 4251)
#include "gdal.h"
#include "gdal_priv.h"

#include "Public/GDALPluginInterface.h"

class GDALPlugin : public FEPluginCore, public GDALPluginInterface
{
public:
	void OnLoad() override;
	void OnUnload() override;
	void OnUpdate(double DeltaTime) override;

	bool SaveFloat32GeoTIFF(const std::string& FilePath, int Width, int Height, int BandCount, const float* Data, const GeoTIFFSettings& Settings) override;
};