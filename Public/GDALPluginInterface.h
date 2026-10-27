#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

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

	// Integer fields are read as int64_t, Real fields as double, String and Other fields as std::string.
	enum class VectorFieldType
	{
		Integer,
		Real,
		String,
		// For example date, time or list fields.
		Other
	};

	struct VectorFieldDefinition
	{
		std::string Name;
		VectorFieldType Type = VectorFieldType::Other;
		// Name of GDAL field type, for example "Integer64" or "Date".
		std::string TypeName;
		int Width = 0;
		int Precision = 0;
	};

	using VectorFieldValue = std::variant<int64_t, double, std::string>;

	struct VectorPoint
	{
		double X = 0.0;
		double Y = 0.0;
		// 0 if geometry has no Z.
		double Z = 0.0;
	};

	struct VectorPolygon
	{
		std::vector<VectorPoint> ExteriorRing;
		// Holes of polygon.
		std::vector<std::vector<VectorPoint>> InteriorRings;
	};

	enum class VectorGeometryType
	{
		// Feature has no geometry or its geometry is not read, for example geometry collection or curve.
		None,
		Point,
		Line,
		Polygon
	};

	// Multi part geometry has one entry for every part, for example multi polygon has one entry in Polygons for every polygon.
	struct VectorFeature
	{
		VectorGeometryType GeometryType = VectorGeometryType::None;
		std::vector<VectorPoint> Points;
		std::vector<std::vector<VectorPoint>> Lines;
		std::vector<VectorPolygon> Polygons;

		// Fields that are not set or are null are missing.
		std::unordered_map<std::string, VectorFieldValue> Fields;

		// DefaultValue is returned if field is missing or has other type.
		template<typename T>
		T GetField(const std::string& Name, T DefaultValue = {}) const
		{
			auto FieldIterator = Fields.find(Name);
			if (FieldIterator == Fields.end())
				return DefaultValue;

			if (const T* Value = std::get_if<T>(&FieldIterator->second))
				return *Value;

			return DefaultValue;
		}
	};

	struct VectorLayer
	{
		std::string Name;
		// Name of GDAL geometry type of layer, for example "Line String".
		std::string GeometryTypeName;
		// Empty if layer has no spatial reference.
		std::string ProjectionWKT;
		std::vector<VectorFieldDefinition> FieldDefinitions;
		std::vector<VectorFeature> Features;

		// Bounds of points of all features, bHasBounds is false if layer has no points.
		bool bHasBounds = false;
		VectorPoint BoundsMin;
		VectorPoint BoundsMax;

		bool HasField(const std::string& Name) const
		{
			for (size_t i = 0; i < FieldDefinitions.size(); i++)
			{
				if (FieldDefinitions[i].Name == Name)
					return true;
			}

			return false;
		}
	};

	struct VectorFileData
	{
		std::string FilePath;
		// Shapefile has one layer, other formats could have several.
		std::vector<VectorLayer> Layers;
	};

	// Order of virtual functions is part of the interface, new functions should be added only at the end.
	virtual ~GDALPluginInterface() = default;
	virtual int GetInterfaceVersion() const { return InterfaceVersion; }

	// Data is pixel interleaved: Width * Height * BandCount floats, first row is the top of the image.
	virtual bool SaveFloat32GeoTIFF(const std::string& FilePath, int Width, int Height, int BandCount, const float* Data, const GeoTIFFSettings& Settings) = 0;
	// Same as SaveFloat32GeoTIFF with one byte per band, for example RGBA image has BandCount 4.
	virtual bool SaveByteGeoTIFF(const std::string& FilePath, int Width, int Height, int BandCount, const uint8_t* Data, const GeoTIFFSettings& Settings) = 0;

	// Reads all layers of vector file in any format that GDAL supports, for example shapefile.
	// Returned data is owned by plugin and is valid until next read, nullptr if file could not be read.
	virtual const VectorFileData* ReadVectorFile(const std::string& FilePath) = 0;
};