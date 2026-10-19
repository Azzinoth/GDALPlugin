#pragma once
#include <string>

// Public API of GDALPlugin, applications include only this header.
// Usage:
//	GDALPluginInterface* GDAL = PLUGIN_MANAGER.GetPlugin<GDALPluginInterface>(UNIQUE_ID.FromString(GDALPluginInterface::PluginID));
//	if (GDAL != nullptr && GDAL->GetInterfaceVersion() == GDALPluginInterface::InterfaceVersion)
//		GDAL->SaveFloat32GeoTIFF(...);
class GDALPluginInterface
{
public:
	// Same as "ID" in GDALPlugin.json.
	static constexpr const char* PluginID = "e3e1ca54-6794-41c2-bbe0-85f68f6dc76c";

	// Increase when anything in this file changes.
	static constexpr int InterfaceVersion = 1;

	struct GeoTIFFSettings
	{
		double GeoTransform[6] = { 0.0, 1.0, 0.0, 0.0, 0.0, -1.0 };
		// Empty means that file will have no projection.
		std::string ProjectionWKT;
		bool bHasNoDataValue = false;
		double NoDataValue = 0.0;
	};

	// Order of virtual functions is part of the interface, new functions should be added only at the end.
	virtual ~GDALPluginInterface() = default;
	virtual int GetInterfaceVersion() const { return InterfaceVersion; }

	// Data is pixel interleaved: Width * Height * BandCount floats, first row is the top of the image.
	virtual bool SaveFloat32GeoTIFF(const std::string& FilePath, int Width, int Height, int BandCount, const float* Data, const GeoTIFFSettings& Settings) = 0;
};