# WCS Basemap Tab — Implementation Plan

**Feature:** Add a *Web Coverage Service (WCS)* tab to `AddBasemapDialog`, giving users a
fourth basemap source alongside XYZ, WMS/WMTS, and ArcGIS REST.

---

## 1. Background & Scope

A WCS server returns raw geospatial raster data (GeoTIFF, NetCDF, …) rather than
pre-rendered imagery.  The rendering path is therefore closer to `GISRasterLayer`
(GDAL decode → paint) than to `WMSLayer` (QImage decode → paint).  For this
implementation the **WCS 1.1.x / 2.0 protocol** is targeted — both share a compatible
URL structure and XML namespace — with a graceful fallback for 1.0.0 servers.

**In scope:**
- `GetCapabilities` → parse coverage list
- `DescribeCoverage` → parse per-coverage metadata (CRS, extent, supported formats,
  axis/field identifiers for range subsets)
- `GetCoverage` → fetch and render single-image coverage (WMS-pattern, not tiled)
- New tab in `AddBasemapDialog`
- New `WCSConnection` struct + `BasemapConnectionStore` round-trips
- New `WCSLayer` class
- Unit tests mirroring the existing `test_basemapconnectionstore` pattern

**Out of scope (tracked as follow-ups):**
- WCS tiled profiles (WMTS-style grid over a coverage)
- Time dimension / elevation dimension subsetting
- Async streaming of large coverages

---

## 2. New Files

```
include/
  layers/wcslayer.h
  connections/basemapconnection.h     ← add WCSConnection struct (existing file)
  ui/dialogs/addbasemapdialog.h       ← add WCS member variables (existing file)

src/
  layers/wcslayer.cpp
  ui/dialogs/addbasemapdialog.cpp     ← add WCS tab + slots (existing file)
  connections/basemapconnectionstore.cpp  ← add WCS save/load/remove (existing file)

tests/unit/
  test_wcsconnectionstore.cpp

tests/gui/
  test_wcslayer.cpp
```

---

## 3. Data Structures

### 3a. Connection struct (`include/connections/basemapconnection.h`)

Add alongside the existing `WMSConnection` and `ArcGISRestConnection` structs:

```cpp
/*!
 * \struct WCSConnection
 * \brief Parameters for an OGC Web Coverage Service (WCS 1.1.x / 2.0).
 *
 * At connect time GetCapabilities is fetched to enumerate coverages, then
 * DescribeCoverage is fetched for the selected coverage to obtain the native
 * CRS, extent, and available fields.
 */
struct WCSConnection
{
    QString            name;
    QString            url;                  ///< Base service URL (no query string)
    QString            version     = QStringLiteral("2.0.1"); ///< Negotiated at connect time

    // --- Coverage selection (populated after Connect + DescribeCoverage) ---
    QString            coverageId;           ///< Selected coverage identifier
    QString            outputCrs    = QStringLiteral("EPSG:4326");
    QString            outputFormat = QStringLiteral("image/tiff"); ///< GDAL-readable
    QString            rangeSubset;          ///< Optional: "band[1]" etc. — empty = all

    // --- Advanced ---
    QString            interpolation = QStringLiteral("nearest");
    BasemapHttpHeaders httpHeaders;
};
```

### 3b. Service-info structs (new header or alongside WMS structs)

```cpp
struct WCSFieldInfo {
    QString identifier;   ///< e.g. "singleBand", "Red", "Green", "Blue"
    QString description;
};

struct WCSCoverageInfo {
    QString          identifier;     ///< Coverage ID, e.g. "dem_1m"
    QString          title;
    QString          abstractText;
    MapExtent        wgs84BoundingBox;         ///< Always in EPSG:4326
    QStringList      supportedCrs;             ///< e.g. {"EPSG:4326", "EPSG:3857"}
    QStringList      supportedFormats;         ///< e.g. {"image/tiff", "application/netcdf"}
    QList<WCSFieldInfo> fields;                ///< From DescribeCoverage <Field> elements
    QStringList      interpolations;           ///< From DescribeCoverage; may be empty
};

struct WCSServiceInfo {
    QString                 title;
    QString                 abstractText;
    QString                 version;           ///< "1.1.1", "2.0.1", etc.
    QList<WCSCoverageInfo>  coverages;
};
```

---

## 4. `WCSLayer` Class

### 4a. Header (`include/layers/wcslayer.h`)

Model directly after `WMSLayer` — single cached raster per viewport, no tile grid.

```cpp
class WCSLayer : public OpenSWMMVisLayer
{
    Q_OBJECT
public:
    explicit WCSLayer(const QUrl &serviceUrl, OpenSWMMVisWorkspace *parent = nullptr);
    ~WCSLayer() override;

    // --- Configuration ---
    void setServiceInfo(const WCSServiceInfo &info);
    [[nodiscard]] WCSServiceInfo serviceInfo() const;

    void setActiveCoverageId(const QString &id);
    void setOutputCrs(const QString &crs);
    void setOutputFormat(const QString &format);
    void setRangeSubset(const QString &subset);
    void setInterpolation(const QString &interp);

    // --- OpenSWMMVisLayer interface ---
    [[nodiscard]] bool isRasterLayer()  const override { return true; }
    [[nodiscard]] bool isBasemapLayer() const override { return true; }

    void fetchCache(const MapExtent &, const QSize &, const SpatialReferenceSystem *) override;
    void render(QPainter *, const MapExtent &, const QSize &, const SpatialReferenceSystem *) override;
    void populateScene(QGraphicsScene *, const MapExtent &, const SpatialReferenceSystem *) override {}
    void onCanvasCRSChanged(const SpatialReferenceSystem *) override;

    // --- Capabilities fetch (used by dialog) ---
    void fetchCapabilities();

signals:
    void capabilitiesFetched(const WCSServiceInfo &info);
    void capabilitiesError(const QString &error);

private slots:
    void onCapabilitiesReply(QNetworkReply *reply);
    void onGetCoverageReply(QNetworkReply *reply);

private:
    QString buildGetCoverageUrl(const MapExtent &extent,
                                const QString   &requestCrs,
                                const QSize     &size) const;
    QString negotiateVersion(const QByteArray &capsXml) const;
    WCSServiceInfo parseCapabilities(const QByteArray &xml) const;
    WCSCoverageInfo parseDescribeCoverage(const QByteArray &xml) const;

    QUrl                          m_serviceUrl;
    QString                       m_version     = QStringLiteral("2.0.1");
    WCSServiceInfo                m_serviceInfo;
    QString                       m_coverageId;
    QString                       m_outputCrs   = QStringLiteral("EPSG:4326");
    QString                       m_outputFormat = QStringLiteral("image/tiff");
    QString                       m_rangeSubset;
    QString                       m_interpolation = QStringLiteral("nearest");

    QNetworkAccessManager        *m_nam          = nullptr;

    // Cached coverage (WMS-style: one image per viewport)
    QImage                        m_cachedImage;
    MapExtent                     m_cacheExtent;
    QSize                         m_cacheSize;

    // In-flight guard (like WMSLayer::m_requestedExtent)
    MapExtent                     m_requestedExtent;
    QSize                         m_requestedSize;

    // OGR transforms (canvas ↔ coverage CRS)
    OGRSpatialReference          *m_wgs84        = nullptr;
    OGRCoordinateTransformation  *m_canvasToReq  = nullptr;
    OGRCoordinateTransformation  *m_reqToCanvas  = nullptr;
    mutable QMutex                m_transformMutex;
};
```

### 4b. Implementation notes (`src/layers/wcslayer.cpp`)

**`fetchCapabilities()`**
```
GET {url}?SERVICE=WCS&REQUEST=GetCapabilities&VERSION=2.0.1
→ parse WCS 2.0 XML (ows:OperationsMetadata, wcs:Contents/wcs:CoverageSummary)
→ For WCS 1.1.x: parse wcs:Capabilities/wcs:ContentMetadata/wcs:CoverageOfferingBrief
→ emit capabilitiesFetched(info) or capabilitiesError(msg)
```

> **Version negotiation:** WCS 2.0 servers reject unknown version strings with an
> `ExceptionReport`.  Send `VERSION=2.0.1` first; if the server replies with a 1.1.x
> `OGC:ServiceExceptionReport`, retry with `VERSION=1.1.2`.

**`fetchCache()`**  (mirrors `WMSLayer::requestTile`)
```
1. Reproject canvas extent → m_outputCrs via OGR (same pattern as WMS)
2. Build GetCoverage URL (see §4c)
3. QNetworkRequest → m_nam->get()
4. Guard against redundant fetches: skip if requested extent+size unchanged
```

**`onGetCoverageReply()`**
```
1. Read raw bytes from reply (typically GeoTIFF)
2. Write to QTemporaryFile
3. Open with GDAL: GDALOpen(tmpPath, GA_ReadOnly)
4. GDALDataset → GDALRasterBand → read into float buffer
5. Normalise values → QImage::Format_ARGB32 (colourmap or greyscale)
6. Apply SRS and extent from dataset metadata
7. m_cachedImage = img; m_cacheExtent = ...; emit repaintRequested()
```

> **Why GDAL, not QImage::loadFromData():** WCS responses are GeoTIFF or NetCDF
> with embedded georeferencing.  GDAL is already a project dependency; `QImage` does
> not handle multi-band rasters or floating-point data.

**`render()`**  (identical logic to `WMSLayer::render`)
```
1. Check m_cachedImage.isNull()
2. Compute dst rect in canvas pixels from m_cacheExtent
3. painter->drawImage(dstRect, m_cachedImage)
```

### 4c. GetCoverage URL construction

**WCS 2.0:**
```
{url}?SERVICE=WCS
      &VERSION=2.0.1
      &REQUEST=GetCoverage
      &COVERAGEID={coverageId}
      &FORMAT={outputFormat}
      &SUBSETTINGCRS={requestCrs}
      &OUTPUTCRS={outputCrs}
      &SUBSET=Lon({xMin},{xMax})
      &SUBSET=Lat({yMin},{yMax})
      &SCALESIZE=Lon({width}),Lat({height})
      [&RANGESUBSET={rangeSubset}]
      [&INTERPOLATION={interpolation}]
```

**WCS 1.1.x fallback:**
```
{url}?SERVICE=WCS
      &VERSION=1.1.2
      &REQUEST=GetCoverage
      &IDENTIFIER={coverageId}
      &FORMAT={outputFormat}
      &BoundingBox={xMin},{yMin},{xMax},{yMax},{crsUrn}
      &GRIDBASECRS={crsUrn}
      &GRIDCS=urn:ogc:def:cs:OGC::0.0.0:CS0.0
      &GRIDTYPE=urn:ogc:def:method:WCS:1.1:2dGridIn2dCrs
      &GRIDORIGIN={xMin},{yMax}
      &GRIDOFFSETS={xRes},-{yRes}
      [&RangeSubset={rangeSubset}]
```

> WCS 1.1.x requires GridOrigin + GridOffsets instead of a pixel count.  Derive from
> BBOX and desired image size: `xRes = width / pixelCols`, `yRes = height / pixelRows`.

---

## 5. AddBasemapDialog Changes

### 5a. New tab

Add a fourth tab between WMS/WMTS and ArcGIS REST:

```
Tab 0: XYZ Tiles
Tab 1: WMS / WMTS
Tab 2: WCS          ← new
Tab 3: ArcGIS REST
```

Update `createLayer()` switch to `case 3: return buildArcGISLayer(parent)` and add
`case 2: return buildWCSLayer(parent)`.

### 5b. WCS tab UI layout

```
[Saved connections ▾]  [New] [Edit] [Delete]
─────────────────────────────────────────────
URL:  [__________________________________] [Connect]
Status: (Connecting… / Connected. / Error: …)

┌─ Available Coverages ─────┐ ┌─ Coverage Options ────────┐
│  Coverage tree/list        │ │  Format:       [combo]    │
│  (populated after connect) │ │  Output CRS:   [combo]    │
│                            │ │  Range subset: [line edit]│
│                            │ │  Interpolation:[combo]    │
└────────────────────────────┘ └───────────────────────────┘

[Authentication (Basic)]  Username: [___]  Password: [___] [Show]
[HTTP Headers widget]
```

### 5c. New member variables (`include/ui/dialogs/addbasemapdialog.h`)

```cpp
// WCS tab
QComboBox  *m_wcsCombo     = nullptr;
QPushButton *m_wcsNew      = nullptr;
QPushButton *m_wcsEdit     = nullptr;
QPushButton *m_wcsDel      = nullptr;
QLineEdit   *m_wcsUrl      = nullptr;
QPushButton *m_wcsConnect  = nullptr;
QLabel      *m_wcsStatus   = nullptr;
QTreeWidget *m_wcsCovTree  = nullptr;
QComboBox   *m_wcsFmt      = nullptr;
QComboBox   *m_wcsCrs      = nullptr;
QLineEdit   *m_wcsRange    = nullptr;   // range subset string
QComboBox   *m_wcsInterp   = nullptr;
QGroupBox   *m_wcsAuthBox  = nullptr;
QLineEdit   *m_wcsUser     = nullptr;
QLineEdit   *m_wcsPass     = nullptr;
QPushButton *m_wcsEye      = nullptr;
BasemapHttpHeadersWidget *m_wcsHeaders = nullptr;

WCSServiceInfo *m_wcsInfo  = nullptr;  // owned, deleted in destructor
```

### 5d. New slots

```cpp
// Mirrors WMS slots exactly
void onWCSConnectionSelected(const QString &name);
void onWCSNew();
void onWCSDelete();
void onWCSConnect();
void onWCSCapabilitiesFetched(const WCSServiceInfo &info);
void onWCSCapabilitiesError(const QString &error);
void onWCSCoverageSelectionChanged();

// Builder
OpenSWMMVisLayer *buildWCSLayer(QObject *parent) const;
```

### 5e. `onWCSConnect()` flow

```cpp
void AddBasemapDialog::onWCSConnect()
{
    const QString url = m_wcsUrl->text().trimmed();
    if (url.isEmpty()) return;

    m_wcsStatus->setText(tr("Connecting…"));
    auto *layer = new WCSLayer(QUrl(url), nullptr);

    connect(layer, &WCSLayer::capabilitiesFetched, this,
            &AddBasemapDialog::onWCSCapabilitiesFetched);
    connect(layer, &WCSLayer::capabilitiesError, this,
            &AddBasemapDialog::onWCSCapabilitiesError);
    connect(layer, &WCSLayer::capabilitiesFetched, layer, &QObject::deleteLater);
    connect(layer, &WCSLayer::capabilitiesError,   layer, &QObject::deleteLater);

    connect(layer, &WCSLayer::capabilitiesFetched, this, [this, layer]() {
        delete m_wcsInfo;
        m_wcsInfo = new WCSServiceInfo(layer->serviceInfo());
        populateWCSTree(*m_wcsInfo);
    });

    layer->fetchCapabilities();
}
```

### 5f. `buildWCSLayer()`

```cpp
OpenSWMMVisLayer *AddBasemapDialog::buildWCSLayer(QObject *parent) const
{
    if (!m_wcsInfo) return nullptr;
    const QList<QTreeWidgetItem *> sel = m_wcsCovTree->selectedItems();
    if (sel.isEmpty()) return nullptr;

    const QString covId = sel.first()->data(0, Qt::UserRole).toString();
    auto *layer = new WCSLayer(QUrl(m_wcsUrl->text().trimmed()),
                               qobject_cast<OpenSWMMVisWorkspace *>(parent));
    layer->setServiceInfo(*m_wcsInfo);
    layer->setActiveCoverageId(covId);
    layer->setOutputFormat(m_wcsFmt->currentText());
    layer->setOutputCrs(m_wcsCrs->currentText());
    layer->setRangeSubset(m_wcsRange->text().trimmed());
    layer->setInterpolation(m_wcsInterp->currentText());
    layer->setHttpHeaders(m_wcsHeaders->headers());
    if (m_wcsAuthBox->isChecked() && !m_wcsUser->text().isEmpty())
        layer->setBasicAuth(m_wcsUser->text(), m_wcsPass->text());

    for (const WCSCoverageInfo &ci : m_wcsInfo->coverages) {
        if (ci.identifier == covId) {
            layer->setName(ci.title.isEmpty() ? ci.identifier : ci.title);
            break;
        }
    }

    // Persist connection
    if (!m_wcsUrl->text().isEmpty()) {
        WCSConnection conn;
        conn.name         = layer->name();
        conn.url          = m_wcsUrl->text().trimmed();
        conn.version      = m_wcsInfo->version;
        conn.coverageId   = covId;
        conn.outputFormat = m_wcsFmt->currentText();
        conn.outputCrs    = m_wcsCrs->currentText();
        conn.rangeSubset  = m_wcsRange->text().trimmed();
        conn.interpolation = m_wcsInterp->currentText();
        conn.httpHeaders  = m_wcsHeaders->headers();
        BasemapAuth auth;
        if (m_wcsAuthBox->isChecked()) {
            auth.username = m_wcsUser->text();
            auth.password = m_wcsPass->text();
        }
        BasemapConnectionStore::instance()->saveWCS(conn, auth);
    }
    return layer;
}
```

---

## 6. Connection Store (`BasemapConnectionStore`)

Add three methods mirroring the WMS API:

```cpp
// Persist
void saveWCS(const WCSConnection &conn, const BasemapAuth &auth);

// Load
WCSConnection  loadWCS(const QString &name) const;
BasemapAuth    loadWCSAuth(const QString &name) const;

// Enumerate / remove
QStringList    wcsConnectionNames() const;
void           removeWCS(const QString &name);
```

**QSettings key layout** (under `BasemapConnections/WCS/{name}/`):

| Key | Value |
|-----|-------|
| `url` | service base URL |
| `version` | negotiated version string |
| `coverageId` | selected coverage |
| `outputCrs` | output CRS identifier |
| `outputFormat` | MIME type |
| `rangeSubset` | raw range subset string |
| `interpolation` | method name |
| `http-header/{key}` | HTTP header values |
| `auth/username` | plain text |
| `auth/password` | AES-256-CBC encrypted (via `BasemapCrypto`) |

---

## 7. Capability XML Parsing

WCS has three major schema versions with distinct XML namespaces.  The parser must
detect the version from the root element before walking the tree.

| Version | Root element | Namespace | Coverage list element |
|---------|-------------|-----------|----------------------|
| 1.0.0 | `WCS_Capabilities` | (none) | `CoverageOfferingBrief` |
| 1.1.x | `Capabilities` | `wcs=http://…/wcs/1.1` | `CoverageSummary` |
| 2.0.x | `Capabilities` | `wcs=http://…/wcs/2.0` | `CoverageSummary` |

Use `QXmlStreamReader` — consistent with the WMS/WMTS parsers already in the project.

```cpp
WCSServiceInfo WCSLayer::parseCapabilities(const QByteArray &xml) const
{
    QXmlStreamReader r(xml);
    WCSServiceInfo info;

    while (!r.atEnd()) {
        r.readNext();
        if (!r.isStartElement()) continue;
        const QStringRef ns  = r.namespaceUri();
        const QStringRef tag = r.name();

        // Detect version from namespace URI
        if (info.version.isEmpty()) {
            if (ns.contains(QStringLiteral("wcs/2.0")))
                info.version = QStringLiteral("2.0.1");
            else if (ns.contains(QStringLiteral("wcs/1.1")))
                info.version = QStringLiteral("1.1.2");
            else if (tag == QStringLiteral("WCS_Capabilities"))
                info.version = QStringLiteral("1.0.0");
        }

        if (tag == QStringLiteral("ServiceTitle") || tag == QStringLiteral("Title"))
            info.title = r.readElementText();

        if (tag == QStringLiteral("CoverageSummary")
            || tag == QStringLiteral("CoverageOfferingBrief"))
            info.coverages << parseCoverageSummary(r, info.version);
    }
    return info;
}
```

---

## 8. Project File Changes

### `CMakeLists.txt` (root)

Add new source files to the `SWMMVis` target sources list:

```cmake
src/layers/wcslayer.cpp
include/layers/wcslayer.h
```

### `tests/unit/CMakeLists.txt`

```cmake
add_swmmvis_unit_test(test_wcsconnectionstore
    test_wcsconnectionstore.cpp
    ${PROJECT_SOURCE_DIR}/src/core/basemapcrypto.cpp
    ${PROJECT_SOURCE_DIR}/src/connections/basemapconnectionstore.cpp
)
if(OpenSSL_FOUND OR OPENSSL_FOUND)
    target_link_libraries(test_wcsconnectionstore PRIVATE OpenSSL::SSL OpenSSL::Crypto)
endif()
```

### `tests/gui/CMakeLists.txt`

```cmake
find_package(Qt6 REQUIRED COMPONENTS Network)
add_swmmvis_gui_test(test_wcslayer
    test_wcslayer.cpp
    ${CMAKE_SOURCE_DIR}/src/layers/wcslayer.cpp
    ${CMAKE_SOURCE_DIR}/include/layers/wcslayer.h
    ${CMAKE_SOURCE_DIR}/src/layers/openswmmvislayer.cpp
    ${CMAKE_SOURCE_DIR}/include/layers/openswmmvislayer.h
    ${CMAKE_SOURCE_DIR}/src/map/mapextent.cpp
    ${CMAKE_SOURCE_DIR}/include/map/mapextent.h
    ${CMAKE_SOURCE_DIR}/src/map/spatialreferencesystem.cpp
    ${CMAKE_SOURCE_DIR}/include/map/spatialreferencesystem.h
)
target_link_libraries(test_wcslayer PRIVATE Qt6::Network GDAL::GDAL)
target_include_directories(test_wcslayer PRIVATE ${GDAL_INCLUDE_DIRS})
```

---

## 9. Test Plan

### Unit tests — `test_wcsconnectionstore.cpp`

| Test | Asserts |
|------|---------|
| `WCSRoundTrip` | All struct fields survive QSettings save/load |
| `WCSAuthRoundTrip` | Username/password encrypted and decrypted correctly |
| `WCSEmptyAuthRoundTrip` | Empty auth returns `isEmpty() == true` |
| `WCSHttpHeadersRoundTrip` | Arbitrary header map preserved exactly |
| `WCSNamesAndRemove` | Saved name appears in list; after remove, does not |

### GUI tests — `test_wcslayer.cpp`

| Test | Asserts |
|------|---------|
| `buildGetCoverageUrl_wcs20` | URL contains correct COVERAGEID, SUBSET, SCALESIZE params |
| `buildGetCoverageUrl_wcs11fallback` | URL uses BoundingBox + GridOffsets form |
| `buildGetCoverageUrl_rangeSubset` | RANGESUBSET param present when set |
| `buildGetCoverageUrl_interpolation` | INTERPOLATION param present when set |
| `parseCapabilities_wcs20` | Coverage list parsed; title, identifier, CRS populated |
| `parseCapabilities_wcs11` | 1.1.x namespace handled; coverage list populated |
| `parseCapabilities_wcs10` | 1.0.0 root element detected; coverage list populated |
| `versionNegotiation_fallsBackTo11` | If 2.0 request returns exception, retries with 1.1.2 |
| `renderCachedCoverage` | `render()` draws cached image at correct scale |

---

## 10. Implementation Phases

### Phase 1 — Data layer (no UI)
1. Add `WCSConnection` struct to `basemapconnection.h`
2. Add `WCSServiceInfo` / `WCSCoverageInfo` structs (same header or new
   `wcscoverageinfo.h` alongside the WMS structs)
3. Implement `BasemapConnectionStore` WCS methods + tests
4. Stub out `WCSLayer`: constructor, `fetchCapabilities`, signal definitions — no
   rendering yet

### Phase 2 — Capabilities parsing + URL building
1. Implement `parseCapabilities()` for WCS 2.0 + 1.1.x + 1.0.0
2. Implement `buildGetCoverageUrl()` for both wire formats
3. Version negotiation (retry on ExceptionReport)
4. Write `test_wcslayer` parsing and URL-building tests

### Phase 3 — GetCoverage fetch + GDAL render
1. `fetchCache()` / `onGetCoverageReply()` with GDAL decode
2. `render()` paint cached image
3. OGR transforms for canvas CRS reprojection (copy WMS pattern)
4. Extend `test_wcslayer` with a local mock-server rendering test

### Phase 4 — UI integration
1. Add WCS tab to `AddBasemapDialog`
2. Wire `onWCSConnect()`, populate tree, options combos
3. Implement `buildWCSLayer()` + save to store
4. Manual smoke test against a public WCS endpoint
   (e.g. `https://ows.rasdaman.org/rasdaman/ows` — publicly accessible WCS 2.0 demo)

### Phase 5 — Polish
1. Status-label messaging during connect / retry
2. Disable OK button until a coverage is selected (match WMS behaviour)
3. Populate interpolation combo from `WCSCoverageInfo::interpolations` when
   non-empty, otherwise show standard defaults (nearest, bilinear, bicubic)
4. Grey out Range Subset line-edit when no `<Field>` metadata is returned

---

## 11. Known Risks & Mitigations

| Risk | Mitigation |
|------|-----------|
| WCS version fragmentation (1.0 / 1.1 / 2.0 each have different XML schemas) | Detect from root element / namespace before parsing; provide per-version code paths |
| Large coverage responses (satellite imagery, DEMs) | Honour the `GetCoverage` pixel-count limit; cap requested width/height to viewport pixels (same as WMS) |
| GDAL temporary file I/O latency | Use `VSIMemFileFromBuffer` (GDAL virtual file system) to decode in-memory instead of writing to disk |
| Range subset syntax varies by server | Treat it as a raw passthrough string (user-entered); document the WCS 2.0 standard syntax in the placeholder text |
| Servers that require DescribeCoverage before GetCoverage | Fetch DescribeCoverage lazily after coverage selection, before the first GetCoverage call, to populate fields list |

---

*Last updated: 2026-05-04*
