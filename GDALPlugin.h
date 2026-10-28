#include "SubSystems/FocalEngine/SubSystems/PluginSystem/FEPluginConnector.h"
using namespace FocalEngine;

//#include "CGALDeclarations.h"

#pragma warning (disable: 4251)
#include "gdal.h"
#include "gdal_priv.h"
#include "ogrsf_frmts.h"

#include "Public/GDALPluginInterface.h"

class GDALPlugin : public FEPluginCore, public GDALPluginInterface
{
	// GDAL functions of static library are delay loaded, without loaded GDAL calling them raises exception.
	bool bGDALLoaded = false;
	std::unique_ptr<VectorFileData> LastReadVectorData;

	bool LoadGDAL();
	bool SaveGeoTIFF(const std::string& CallerName, const std::string& FilePath, int Width, int Height, int BandCount, GDALDataType DataType, const void* Data, const GeoTIFFSettings& Settings);

	static void ReadFields(const OGRFeature* Feature, const std::vector<VectorFieldDefinition>& FieldDefinitions, VectorFeature& OutFeature);
	static void ReadGeometry(const OGRGeometry* Geometry, VectorFeature& OutFeature);
	static void ReadPolygon(const OGRPolygon* Polygon, VectorPolygon& OutPolygon);
	static void ReadPoints(const OGRSimpleCurve* Curve, std::vector<VectorPoint>& OutPoints);
	static void AddToBounds(const VectorFeature& Feature, VectorLayer& Layer);
public:
	bool OnLoad() override;
	void OnUnload() override;
	void OnUpdate(double DeltaTime) override;

	bool SaveFloat32GeoTIFF(const std::string& FilePath, int Width, int Height, int BandCount, const float* Data, const GeoTIFFSettings& Settings) override;
	bool SaveByteGeoTIFF(const std::string& FilePath, int Width, int Height, int BandCount, const uint8_t* Data, const GeoTIFFSettings& Settings) override;

	const VectorFileData* ReadVectorFile(const std::string& FilePath) override;
};