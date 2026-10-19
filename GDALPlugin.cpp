#include "GDALPlugin.h"

// DO NOT CHANGE THIS LINE.
// The plugin manager uses it to create and destroy the plugin instance.
REGISTER_PLUGIN(GDALPlugin)

void GDALPlugin::OnLoad()
{
	GDALAllRegister();

	const int DriverCount = GDALGetDriverCount();
	if (DriverCount == 0)
	{
		LOG.Add("GDALPlugin::OnLoad: GDAL did not register any drivers", "GDAL_PLUGIN", FE_LOG_ERROR);
		return;
	}

	LOG.Add("GDALPlugin::OnLoad: " + std::string(GDALVersionInfo("--version")) + " loaded, " + std::to_string(DriverCount) + " drivers registered", "GDAL_PLUGIN");

	// Drivers the plugin relies on.
	if (GDALGetDriverByName("GTiff") == nullptr)
		LOG.Add("GDALPlugin::OnLoad: GTiff driver is not available", "GDAL_PLUGIN", FE_LOG_WARNING);

	if (CPLGetLastErrorType() != CE_None)
		LOG.Add("GDALPlugin::OnLoad: GDAL reported: " + std::string(CPLGetLastErrorMsg()), "GDAL_PLUGIN", FE_LOG_WARNING);
}

void GDALPlugin::OnUnload()
{

}

void GDALPlugin::OnUpdate(double DeltaTime)
{

}

bool GDALPlugin::SaveFloat32GeoTIFF(const std::string& FilePath, int Width, int Height, int BandCount, const float* Data, const GeoTIFFSettings& Settings)
{
	if (FilePath.empty() || Width <= 0 || Height <= 0 || BandCount <= 0 || Data == nullptr)
	{
		LOG.Add("GDALPlugin::SaveFloat32GeoTIFF: Invalid arguments", "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	GDALDriver* Driver = GetGDALDriverManager()->GetDriverByName("GTiff");
	if (Driver == nullptr)
	{
		LOG.Add("GDALPlugin::SaveFloat32GeoTIFF: GTiff driver is not available", "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	GDALDataset* Dataset = Driver->Create(FilePath.c_str(), Width, Height, BandCount, GDT_Float32, nullptr);
	if (Dataset == nullptr)
	{
		LOG.Add("GDALPlugin::SaveFloat32GeoTIFF: Failed to create \"" + FilePath + "\": " + std::string(CPLGetLastErrorMsg()), "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	Dataset->SetGeoTransform(GDALGeoTransform(Settings.GeoTransform));
	if (!Settings.ProjectionWKT.empty())
		Dataset->SetProjection(Settings.ProjectionWKT.c_str());

	if (Settings.bHasNoDataValue)
	{
		for (int i = 1; i <= BandCount; i++)
			Dataset->GetRasterBand(i)->SetNoDataValue(Settings.NoDataValue);
	}

	// Pixel interleaved data: bands of one pixel are next to each other.
	const GSpacing PixelSpace = static_cast<GSpacing>(sizeof(float)) * BandCount;
	CPLErr Error = Dataset->RasterIO(GF_Write, 0, 0, Width, Height, const_cast<float*>(Data), Width, Height, GDT_Float32,
		BandCount, nullptr, PixelSpace, PixelSpace * Width, sizeof(float), nullptr);

	if (Error != CE_None)
	{
		LOG.Add("GDALPlugin::SaveFloat32GeoTIFF: Failed to write data to \"" + FilePath + "\": " + std::string(CPLGetLastErrorMsg()), "GDAL_PLUGIN", FE_LOG_ERROR);
		GDALClose(Dataset);
		return false;
	}

	// Data is flushed to file on close, so errors could happen here too.
	if (GDALClose(Dataset) != CE_None)
	{
		LOG.Add("GDALPlugin::SaveFloat32GeoTIFF: Failed to finish writing \"" + FilePath + "\": " + std::string(CPLGetLastErrorMsg()), "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	return true;
}