#include "GDALPlugin.h"
#include <algorithm>

// DO NOT CHANGE THIS LINE.
// The plugin manager uses it to create and destroy the plugin instance.
REGISTER_PLUGIN(GDALPlugin)

bool GDALPlugin::LoadGDAL()
{
#ifdef FOCAL_ENGINE_SHARED
	// Plugin DLL imports GDAL, so GDAL was loaded from plugin folder together with plugin DLL.
	return true;
#else
	// Static library is linked with /DELAYLOAD:gdal.dll, so applications start without GDAL DLLs.
	// gdal.dll is loaded here from plugin folder and delay loaded functions are resolved by name to it.
	// Release GDAL is used in every configuration, because static link file links its import library.
	const std::string GDALPath = FILE_SYSTEM.GetAbsolutePath(GetPluginDirectory() + "Release/gdal.dll");
	if (LoadLibraryExA(GDALPath.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS) != nullptr)
		return true;

	// In games DLLs of plugins are next to executable.
	return LoadLibraryA("gdal.dll") != nullptr;
#endif
}

bool GDALPlugin::OnLoad()
{
	bGDALLoaded = LoadGDAL();
	if (!bGDALLoaded)
	{
		// Plugin stays loaded, its functions fail while GDAL is not loaded.
		LOG.Add("GDALPlugin::OnLoad: Loading GDAL DLL failed, GeoTIFF and vector files are not supported", "GDAL_PLUGIN", FE_LOG_ERROR);
		return true;
	}

	GDALAllRegister();

	const int DriverCount = GDALGetDriverCount();
	if (DriverCount == 0)
	{
		LOG.Add("GDALPlugin::OnLoad: GDAL did not register any drivers", "GDAL_PLUGIN", FE_LOG_ERROR);
		return true;
	}

	LOG.Add("GDALPlugin::OnLoad: " + std::string(GDALVersionInfo("--version")) + " loaded, " + std::to_string(DriverCount) + " drivers registered", "GDAL_PLUGIN");

	// Drivers the plugin relies on.
	if (GDALGetDriverByName("GTiff") == nullptr)
		LOG.Add("GDALPlugin::OnLoad: GTiff driver is not available", "GDAL_PLUGIN", FE_LOG_WARNING);

	if (GDALGetDriverByName("ESRI Shapefile") == nullptr)
		LOG.Add("GDALPlugin::OnLoad: ESRI Shapefile driver is not available", "GDAL_PLUGIN", FE_LOG_WARNING);

	if (CPLGetLastErrorType() != CE_None)
		LOG.Add("GDALPlugin::OnLoad: GDAL reported: " + std::string(CPLGetLastErrorMsg()), "GDAL_PLUGIN", FE_LOG_WARNING);

	return true;
}

void GDALPlugin::OnUnload()
{
	LastReadVectorData.reset();
}

void GDALPlugin::OnUpdate(double DeltaTime)
{

}

bool GDALPlugin::SaveFloat32GeoTIFF(const std::string& FilePath, int Width, int Height, int BandCount, const float* Data, const GeoTIFFSettings& Settings)
{
	return SaveGeoTIFF("GDALPlugin::SaveFloat32GeoTIFF", FilePath, Width, Height, BandCount, GDT_Float32, Data, Settings);
}

bool GDALPlugin::SaveByteGeoTIFF(const std::string& FilePath, int Width, int Height, int BandCount, const uint8_t* Data, const GeoTIFFSettings& Settings)
{
	return SaveGeoTIFF("GDALPlugin::SaveByteGeoTIFF", FilePath, Width, Height, BandCount, GDT_Byte, Data, Settings);
}

bool GDALPlugin::SaveGeoTIFF(const std::string& CallerName, const std::string& FilePath, int Width, int Height, int BandCount, GDALDataType DataType, const void* Data, const GeoTIFFSettings& Settings)
{
	if (!bGDALLoaded)
	{
		LOG.Add(CallerName + ": GDAL is not loaded", "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	if (FilePath.empty() || Width <= 0 || Height <= 0 || BandCount <= 0 || Data == nullptr)
	{
		LOG.Add(CallerName + ": Invalid arguments", "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	GDALDriver* Driver = GetGDALDriverManager()->GetDriverByName("GTiff");
	if (Driver == nullptr)
	{
		LOG.Add(CallerName + ": GTiff driver is not available", "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	GDALDataset* Dataset = Driver->Create(FilePath.c_str(), Width, Height, BandCount, DataType, nullptr);
	if (Dataset == nullptr)
	{
		LOG.Add(CallerName + ": Failed to create \"" + FilePath + "\": " + std::string(CPLGetLastErrorMsg()), "GDAL_PLUGIN", FE_LOG_ERROR);
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
	const GSpacing SampleSize = GDALGetDataTypeSizeBytes(DataType);
	const GSpacing PixelSpace = SampleSize * BandCount;
	CPLErr Error = Dataset->RasterIO(GF_Write, 0, 0, Width, Height, const_cast<void*>(Data), Width, Height, DataType,
		BandCount, nullptr, PixelSpace, PixelSpace * Width, SampleSize, nullptr);

	if (Error != CE_None)
	{
		LOG.Add(CallerName + ": Failed to write data to \"" + FilePath + "\": " + std::string(CPLGetLastErrorMsg()), "GDAL_PLUGIN", FE_LOG_ERROR);
		GDALClose(Dataset);
		return false;
	}

	// Data is flushed to file on close, so errors could happen here too.
	if (GDALClose(Dataset) != CE_None)
	{
		LOG.Add(CallerName + ": Failed to finish writing \"" + FilePath + "\": " + std::string(CPLGetLastErrorMsg()), "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	return true;
}

const GDALPluginInterface::VectorFileData* GDALPlugin::ReadVectorFile(const std::string& FilePath)
{
	LastReadVectorData.reset();
	if (!bGDALLoaded)
	{
		LOG.Add("GDALPlugin::ReadVectorFile: GDAL is not loaded", "GDAL_PLUGIN", FE_LOG_ERROR);
		return nullptr;
	}

	// Without GDAL_OF_VERBOSE_ERROR GDAL does not report why file could not be opened.
	GDALDataset* Dataset = GDALDataset::Open(FilePath.c_str(), GDAL_OF_VECTOR | GDAL_OF_VERBOSE_ERROR);
	if (Dataset == nullptr)
	{
		LOG.Add("GDALPlugin::ReadVectorFile: Failed to open \"" + FilePath + "\": " + std::string(CPLGetLastErrorMsg()), "GDAL_PLUGIN", FE_LOG_ERROR);
		return nullptr;
	}

	std::unique_ptr<VectorFileData> Data = std::make_unique<VectorFileData>();
	Data->FilePath = FilePath;
	for (int i = 0; i < Dataset->GetLayerCount(); i++)
	{
		OGRLayer* Layer = Dataset->GetLayer(i);
		if (Layer == nullptr)
			continue;

		VectorLayer& NewLayer = Data->Layers.emplace_back();
		NewLayer.Name = Layer->GetName();
		NewLayer.GeometryTypeName = OGRGeometryTypeToName(Layer->GetGeomType());

		const OGRSpatialReference* SpatialReference = Layer->GetSpatialRef();
		if (SpatialReference != nullptr)
		{
			char* WKT = nullptr;
			if (SpatialReference->exportToWkt(&WKT) == OGRERR_NONE && WKT != nullptr)
				NewLayer.ProjectionWKT = WKT;

			// Memory allocated by GDAL is freed by GDAL, because it could use other C runtime than plugin.
			CPLFree(WKT);
		}

		OGRFeatureDefn* FeatureDefinition = Layer->GetLayerDefn();
		for (int j = 0; j < FeatureDefinition->GetFieldCount(); j++)
		{
			const OGRFieldDefn* FieldDefinition = FeatureDefinition->GetFieldDefn(j);
			VectorFieldDefinition& NewFieldDefinition = NewLayer.FieldDefinitions.emplace_back();
			NewFieldDefinition.Name = FieldDefinition->GetNameRef();
			NewFieldDefinition.TypeName = OGRFieldDefn::GetFieldTypeName(FieldDefinition->GetType());
			NewFieldDefinition.Width = FieldDefinition->GetWidth();
			NewFieldDefinition.Precision = FieldDefinition->GetPrecision();

			switch (FieldDefinition->GetType())
			{
				case OFTInteger:
				case OFTInteger64:
				{
					NewFieldDefinition.Type = VectorFieldType::Integer;
					break;
				}
				case OFTReal:
				{
					NewFieldDefinition.Type = VectorFieldType::Real;
					break;
				}
				case OFTString:
				{
					NewFieldDefinition.Type = VectorFieldType::String;
					break;
				}
				default:
				{
					NewFieldDefinition.Type = VectorFieldType::Other;
					break;
				}
			}
		}

		Layer->ResetReading();
		OGRFeature* Feature = nullptr;
		while ((Feature = Layer->GetNextFeature()) != nullptr)
		{
			VectorFeature& NewFeature = NewLayer.Features.emplace_back();
			ReadFields(Feature, NewLayer.FieldDefinitions, NewFeature);
			ReadGeometry(Feature->GetGeometryRef(), NewFeature);
			AddToBounds(NewFeature, NewLayer);
			OGRFeature::DestroyFeature(Feature);
		}
	}

	GDALClose(Dataset);
	LastReadVectorData = std::move(Data);
	return LastReadVectorData.get();
}

void GDALPlugin::ReadFields(const OGRFeature* Feature, const std::vector<VectorFieldDefinition>& FieldDefinitions, VectorFeature& OutFeature)
{
	for (int i = 0; i < static_cast<int>(FieldDefinitions.size()); i++)
	{
		if (!Feature->IsFieldSetAndNotNull(i))
			continue;

		const std::string& FieldName = FieldDefinitions[i].Name;
		switch (FieldDefinitions[i].Type)
		{
			case VectorFieldType::Integer:
			{
				OutFeature.Fields[FieldName] = static_cast<int64_t>(Feature->GetFieldAsInteger64(i));
				break;
			}
			case VectorFieldType::Real:
			{
				OutFeature.Fields[FieldName] = Feature->GetFieldAsDouble(i);
				break;
			}
			default:
			{
				OutFeature.Fields[FieldName] = std::string(Feature->GetFieldAsString(i));
				break;
			}
		}
	}
}

void GDALPlugin::ReadGeometry(const OGRGeometry* Geometry, VectorFeature& OutFeature)
{
	if (Geometry == nullptr || Geometry->IsEmpty())
		return;

	switch (wkbFlatten(Geometry->getGeometryType()))
	{
		case wkbPoint:
		{
			const OGRPoint* Point = Geometry->toPoint();
			OutFeature.GeometryType = VectorGeometryType::Point;
			OutFeature.Points.push_back({ Point->getX(), Point->getY(), Point->getZ() });
			break;
		}
		case wkbMultiPoint:
		{
			const OGRMultiPoint* MultiPoint = Geometry->toMultiPoint();
			OutFeature.GeometryType = VectorGeometryType::Point;
			for (int i = 0; i < MultiPoint->getNumGeometries(); i++)
			{
				const OGRPoint* Point = MultiPoint->getGeometryRef(i);
				OutFeature.Points.push_back({ Point->getX(), Point->getY(), Point->getZ() });
			}
			break;
		}
		case wkbLineString:
		{
			OutFeature.GeometryType = VectorGeometryType::Line;
			ReadPoints(Geometry->toLineString(), OutFeature.Lines.emplace_back());
			break;
		}
		case wkbMultiLineString:
		{
			const OGRMultiLineString* MultiLineString = Geometry->toMultiLineString();
			OutFeature.GeometryType = VectorGeometryType::Line;
			for (int i = 0; i < MultiLineString->getNumGeometries(); i++)
				ReadPoints(MultiLineString->getGeometryRef(i), OutFeature.Lines.emplace_back());
			break;
		}
		case wkbPolygon:
		{
			OutFeature.GeometryType = VectorGeometryType::Polygon;
			ReadPolygon(Geometry->toPolygon(), OutFeature.Polygons.emplace_back());
			break;
		}
		case wkbMultiPolygon:
		{
			const OGRMultiPolygon* MultiPolygon = Geometry->toMultiPolygon();
			OutFeature.GeometryType = VectorGeometryType::Polygon;
			for (int i = 0; i < MultiPolygon->getNumGeometries(); i++)
				ReadPolygon(MultiPolygon->getGeometryRef(i), OutFeature.Polygons.emplace_back());
			break;
		}
		default:
			break;
	}
}

void GDALPlugin::ReadPolygon(const OGRPolygon* Polygon, VectorPolygon& OutPolygon)
{
	const OGRLinearRing* ExteriorRing = Polygon->getExteriorRing();
	if (ExteriorRing != nullptr)
		ReadPoints(ExteriorRing, OutPolygon.ExteriorRing);

	OutPolygon.InteriorRings.resize(Polygon->getNumInteriorRings());
	for (int i = 0; i < Polygon->getNumInteriorRings(); i++)
		ReadPoints(Polygon->getInteriorRing(i), OutPolygon.InteriorRings[i]);
}

void GDALPlugin::ReadPoints(const OGRSimpleCurve* Curve, std::vector<VectorPoint>& OutPoints)
{
	OutPoints.resize(Curve->getNumPoints());
	for (int i = 0; i < Curve->getNumPoints(); i++)
		OutPoints[i] = { Curve->getX(i), Curve->getY(i), Curve->getZ(i) };
}

void GDALPlugin::AddToBounds(const VectorFeature& Feature, VectorLayer& Layer)
{
	auto AddPoints = [&Layer](const std::vector<VectorPoint>& Points)
	{
		for (size_t i = 0; i < Points.size(); i++)
		{
			if (!Layer.bHasBounds)
			{
				Layer.BoundsMin = Points[i];
				Layer.BoundsMax = Points[i];
				Layer.bHasBounds = true;
				continue;
			}

			Layer.BoundsMin.X = std::min(Layer.BoundsMin.X, Points[i].X);
			Layer.BoundsMin.Y = std::min(Layer.BoundsMin.Y, Points[i].Y);
			Layer.BoundsMin.Z = std::min(Layer.BoundsMin.Z, Points[i].Z);
			Layer.BoundsMax.X = std::max(Layer.BoundsMax.X, Points[i].X);
			Layer.BoundsMax.Y = std::max(Layer.BoundsMax.Y, Points[i].Y);
			Layer.BoundsMax.Z = std::max(Layer.BoundsMax.Z, Points[i].Z);
		}
	};

	AddPoints(Feature.Points);
	for (size_t i = 0; i < Feature.Lines.size(); i++)
		AddPoints(Feature.Lines[i]);

	// Holes are inside exterior ring, so they do not change bounds.
	for (size_t i = 0; i < Feature.Polygons.size(); i++)
		AddPoints(Feature.Polygons[i].ExteriorRing);
}

bool GDALPlugin::ReadFloat32Array(const std::string& FilePath, const std::string& ArrayName,
								  const std::vector<uint64_t>& Start, const std::vector<uint64_t>& Count, std::vector<float>& Result)
{
	Result.clear();
	if (!bGDALLoaded)
	{
		LOG.Add("GDALPlugin::ReadFloat32Array: GDAL is not loaded", "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	if (FilePath.empty() || ArrayName.empty())
	{
		LOG.Add("GDALPlugin::ReadFloat32Array: Invalid arguments", "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	// Only GDAL C API is used here, static library uses Release GDAL also in Debug, where std::string and std::vector of GDAL C++ API have other layout.
	GDALDatasetH Dataset = GDALOpenEx(FilePath.c_str(), GDAL_OF_MULTIDIM_RASTER | GDAL_OF_VERBOSE_ERROR, nullptr, nullptr, nullptr);
	if (Dataset == nullptr)
	{
		LOG.Add("GDALPlugin::ReadFloat32Array: Failed to open \"" + FilePath + "\": " + std::string(CPLGetLastErrorMsg()), "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	bool bResult = false;
	GDALGroupH RootGroup = GDALDatasetGetRootGroup(Dataset);
	if (RootGroup == nullptr)
	{
		LOG.Add("GDALPlugin::ReadFloat32Array: \"" + FilePath + "\" has no root group", "GDAL_PLUGIN", FE_LOG_ERROR);
	}
	else
	{
		GDALMDArrayH Array = ArrayName[0] == '/' ? GDALGroupOpenMDArrayFromFullname(RootGroup, ArrayName.c_str(), nullptr) : GDALGroupOpenMDArray(RootGroup, ArrayName.c_str(), nullptr);
		if (Array == nullptr)
		{
			LOG.Add("GDALPlugin::ReadFloat32Array: Array \"" + ArrayName + "\" is not found in \"" + FilePath + "\"", "GDAL_PLUGIN", FE_LOG_ERROR);
		}
		else
		{
			bResult = ReadFloat32ArrayValues(Array, FilePath, ArrayName, Start, Count, Result);
			GDALMDArrayRelease(Array);
		}

		GDALGroupRelease(RootGroup);
	}

	GDALClose(Dataset);
	return bResult;
}

bool GDALPlugin::ReadFloat32ArrayValues(GDALMDArrayH Array, const std::string& FilePath, const std::string& ArrayName,
										const std::vector<uint64_t>& Start, const std::vector<uint64_t>& Count, std::vector<float>& Result)
{
	GDALExtendedDataTypeH DataType = GDALMDArrayGetDataType(Array);
	const bool bNumeric = DataType != nullptr && GDALExtendedDataTypeGetClass(DataType) == GEDTC_NUMERIC;
	if (DataType != nullptr)
		GDALExtendedDataTypeRelease(DataType);

	if (!bNumeric)
	{
		LOG.Add("GDALPlugin::ReadFloat32Array: Array \"" + ArrayName + "\" is not numeric", "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	size_t DimensionCount = 0;
	GDALDimensionH* Dimensions = GDALMDArrayGetDimensions(Array, &DimensionCount);
	std::vector<GUInt64> DimensionSizes(DimensionCount);
	for (size_t i = 0; i < DimensionCount; i++)
		DimensionSizes[i] = GDALDimensionGetSize(Dimensions[i]);

	if (Dimensions != nullptr)
		GDALReleaseDimensions(Dimensions, DimensionCount);

	std::vector<GUInt64> ArrayStart(Start.begin(), Start.end());
	std::vector<size_t> ArrayCount(Count.begin(), Count.end());
	if (ArrayStart.empty() && ArrayCount.empty())
	{
		ArrayStart.resize(DimensionCount, 0);
		for (size_t i = 0; i < DimensionCount; i++)
			ArrayCount.push_back(static_cast<size_t>(DimensionSizes[i]));
	}

	if (ArrayStart.size() != DimensionCount || ArrayCount.size() != DimensionCount)
	{
		LOG.Add("GDALPlugin::ReadFloat32Array: Array \"" + ArrayName + "\" has " + std::to_string(DimensionCount) + " dimensions, Start and Count should have one value per dimension", "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	size_t ValueCount = 1;
	for (size_t i = 0; i < DimensionCount; i++)
	{
		const GUInt64 DimensionSize = DimensionSizes[i];
		if (ArrayCount[i] == 0 || ArrayStart[i] >= DimensionSize || ArrayCount[i] > DimensionSize - ArrayStart[i])
		{
			LOG.Add("GDALPlugin::ReadFloat32Array: Start and Count are outside of dimension " + std::to_string(i) + " of array \"" + ArrayName + "\", its size is " + std::to_string(DimensionSize), "GDAL_PLUGIN", FE_LOG_ERROR);
			return false;
		}

		ValueCount *= ArrayCount[i];
	}

	// Exceptions should not leave the plugin, application could be built with other C runtime.
	try
	{
		Result.resize(ValueCount);
	}
	catch (const std::exception&)
	{
		LOG.Add("GDALPlugin::ReadFloat32Array: Not enough memory for " + std::to_string(ValueCount) + " values of array \"" + ArrayName + "\"", "GDAL_PLUGIN", FE_LOG_ERROR);
		return false;
	}

	GDALExtendedDataTypeH BufferDataType = GDALExtendedDataTypeCreate(GDT_Float32);
	const bool bRead = GDALMDArrayRead(Array, ArrayStart.data(), ArrayCount.data(), nullptr, nullptr, BufferDataType, Result.data(), nullptr, 0) != 0;
	GDALExtendedDataTypeRelease(BufferDataType);
	if (!bRead)
	{
		LOG.Add("GDALPlugin::ReadFloat32Array: Failed to read array \"" + ArrayName + "\" of \"" + FilePath + "\": " + std::string(CPLGetLastErrorMsg()), "GDAL_PLUGIN", FE_LOG_ERROR);
		Result.clear();
		return false;
	}

	return true;
}