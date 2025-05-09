#pragma once

#include "data/tileData.h"
#include "gl.h"
#include "gl/uniform.h"
#include "scene/drawRule.h"
#include "util/fastmap.h"

#include <memory>
#include <string>
#include <vector>

namespace Tangram {

class Label;
class LabelCollider;
class Light;
class MapProjection;
class Marker;
class Material;
class RenderState;
class Scene;
class ShaderProgram;
class ShaderSource;
class Style;
class Tile;
class TileSource;
class VertexLayout;
class View;
struct DrawRule;
struct LightUniforms;
struct MaterialUniforms;

enum class StyleType : uint8_t {
    none,
    debug,
    point,
    polygon,
    polyline,
    raster,
    text,
};

enum class LightingType : uint8_t {
    none,
    vertex,
    fragment
};

enum class Blending : uint8_t {
    opaque,
    add,
    multiply,
    inlay,
    translucent,
    overlay,
};

enum class RasterType : uint8_t {
    none,
    color,
    normal,
    custom
};

struct StyledMesh {
    virtual bool draw(RenderState& rs, ShaderProgram& _shader, bool _useVao = true) = 0;
    virtual size_t bufferSize() const = 0;

    virtual ~StyledMesh() {}
};

class StyleBuilder {
public:

    virtual ~StyleBuilder() = default;

    virtual void setup(const Tile& _tile) = 0;

    virtual void setup(const Marker& _marker, int zoom) = 0;

    virtual bool addFeature(const Feature& _feat, const DrawRule& _rule);

    /* Build styled vertex data for point geometry */
    virtual bool addPoint(const Point& _point, const Properties& _props, const DrawRule& _rule);

    /* Build styled vertex data for line geometry */
    virtual bool addLine(const Line& _line, const Properties& _props, const DrawRule& _rule);

    /* Build styled vertex data for polygon geometry */
    virtual bool addPolygon(const Polygon& _polygon, const Properties& _props, const DrawRule& _rule);

    /* Create a new mesh object using the vertex layout corresponding to this style */
    virtual std::unique_ptr<StyledMesh> build() = 0;

    virtual bool checkRule(const DrawRule& _rule) const;

    virtual void addLayoutItems(LabelCollider& _layout) {}

    virtual void addSelectionItems(LabelCollider& _layout) {}

    virtual const Style& style() const = 0;
};

/* Means of constructing and rendering map geometry
 *
 * A Style defines a way to
 *   1. Construct map geometry into a mesh for drawing and
 *   2. Render the resulting mesh in a scene
 * Style implementations must provide functions to construct
 * a <VertexLayout> for their geometry, construct a <ShaderProgram>
 * for rendering meshes, and build point, line, and polygon
 * geometry into meshes. See <PolygonStyle> for a basic implementation.
 */
class Style {

using StyleUniform = std::pair<UniformLocation, UniformValue >;

protected:

    /* The platform pixel scale */
    float m_pixelScale = 1.0;

    /* Unique name for a style instance */
    std::string m_name;
    uint32_t m_id = 0;

    std::unique_ptr<ShaderSource> m_shaderSource;

    /* <ShaderProgram> used to draw meshes using this style */
    std::shared_ptr<ShaderProgram> m_shaderProgram;

    std::shared_ptr<ShaderProgram> m_selectionProgram;

    /* <VertexLayout> shared between meshes using this style */
    std::shared_ptr<VertexLayout> m_vertexLayout;

    /* Stores default style draw rules*/
    std::unique_ptr<DrawRuleData> m_defaultDrawRule = nullptr;

    /* <LightingType> to determine how lighting will be calculated for this style */
    LightingType m_lightingType = LightingType::fragment;

    Blending m_blend = Blending::opaque;
    int m_blendOrder = -1;

    /* Draw mode to pass into <Mesh>es created with this style */
    GLenum m_drawMode;

    /* animated property */
    bool m_animated = false;

    /* Whether the style should generate texture coordinates */
    bool m_texCoordsGeneration = false;

    bool m_hasColorShaderBlock = false;

    RasterType m_rasterType = RasterType::none;

    bool m_selection;

    StyleType m_type = StyleType::none;

    struct UniformBlock {
        UniformLocation uTime{"u_time"};
        // View uniforms
        UniformLocation uDevicePixelRatio{"u_device_pixel_ratio"};
        UniformLocation uResolution{"u_resolution"};
        UniformLocation uMapPosition{"u_map_position"};
        UniformLocation uNormalMatrix{"u_normal_matrix"};
        UniformLocation uInverseNormalMatrix{"u_inverse_normal_matrix"};
        UniformLocation uMetersPerPixel{"u_meters_per_pixel"};
        UniformLocation uView{"u_view"};
        UniformLocation uProj{"u_proj"};
        // Tile uniforms
        UniformLocation uModel{"u_model"};
        UniformLocation uTileOrigin{"u_tile_origin"};
        UniformLocation uProxyDepth{"u_proxy_depth"};
        UniformLocation uRasters{"u_rasters"};
        UniformLocation uRasterSizes{"u_raster_sizes"};
        UniformLocation uRasterOffsets{"u_raster_offsets"};

        std::vector<StyleUniform> styleUniforms;
    } m_mainUniforms, m_selectionUniforms;

    /* Set uniform values when @_updateUniforms is true,
     */
    void setupSceneShaderUniforms(RenderState& rs, UniformBlock& _uniformBlock);

    void setupShaderUniforms(RenderState& rs, ShaderProgram& _program, const View& _view,
                             UniformBlock& _uniformBlock);

    struct LightHandle {
        LightHandle(Light* _light, std::unique_ptr<LightUniforms> _uniforms);
        Light *light;
        std::unique_ptr<LightUniforms> uniforms;
    };


    struct MaterialHandle {
        /* <Material> used for drawing meshes that use this style */
        std::shared_ptr<Material> material;
        std::unique_ptr<MaterialUniforms> uniforms;
    };

    std::vector<LightHandle> m_lights;
    MaterialHandle m_material;

public:

    Style(std::string _name, Blending _blendMode, GLenum _drawMode, bool _selection);

    virtual ~Style();

    StyleType type() { return m_type; }

    std::string getTypeName() ;

    static bool compare(std::unique_ptr<Style>& a, std::unique_ptr<Style>& b) {

        const auto& modeA = a->blendMode();
        const auto& modeB = b->blendMode();
        const auto& orderA = a->blendOrder();
        const auto& orderB = b->blendOrder();

        if (modeA != Blending::opaque && modeB != Blending::opaque) {
            if (orderA != orderB) {
                return orderA < orderB;
            }
        }
        if (modeA != modeB) {
            return static_cast<uint8_t>(modeA) < static_cast<uint8_t>(modeB);
        }
        return a->getName() < b->getName();
    }

    static const std::vector<std::string>& builtInStyleNames();

    Blending blendMode() const { return m_blend; };
    int blendOrder() const { return m_blendOrder; };

    void setBlendMode(Blending _blendMode) { m_blend = _blendMode; }
    void setBlendOrder(int _blendOrder) { m_blendOrder = _blendOrder; }

    /* Whether or not the style is animated */
    bool isAnimated() { return m_animated; }

    /* Make this style ready to be used (call after all needed properties are set) */
    virtual void build(const Scene& _scene);

    virtual void onBeginUpdate() {}

    virtual void onBeginFrame(RenderState& rs) {}

    /* Create <VertexLayout> corresponding to this style; subclasses must
     * implement this and call it on construction
     */
    virtual void constructVertexLayout() = 0;

    /* Create <ShaderProgram> for this style; subclasses must implement
     * this and call it on construction
     */
    virtual void constructShaderProgram() = 0;

    /* Perform any setup needed before drawing each frame
     * _textUnit is the next available texture unit
     */
    virtual void onBeginDrawFrame(RenderState& rs, const View& _view);

    virtual void onBeginDrawSelectionFrame(RenderState& rs, const View& _view);

    /* Perform any unsetup needed after drawing each frame */
    virtual void onEndDrawFrame(RenderState& rs, const View& _view) {}

    /* Draws the geometry associated with this <Style>
     * returns true when meshes associated with this style are successfully drawn,
     * false otherwise.
     */
    virtual bool draw(RenderState& rs, const Tile& _tile);

    virtual bool draw(RenderState& rs, const Marker& _marker);

    virtual bool draw(RenderState& rs, const View& _view,
                      const std::vector<std::shared_ptr<Tile>>& _tiles,
                      const std::vector<std::unique_ptr<Marker>>& _markers);

    void drawSelectionFrame(RenderState& rs, const View& _view,
                            const std::vector<std::shared_ptr<Tile>>& _tiles,
                            const std::vector<std::unique_ptr<Marker>>& _markers);

    void drawSelectionFrame(RenderState& rs, const Tile& _tile);

    void drawSelectionFrame(RenderState& _rs, const Marker& _marker);

    virtual void setLightingType(LightingType _lType);

    void setAnimated(bool _animated) { m_animated = _animated; }

    virtual void setPixelScale(float _pixelScale) { m_pixelScale = _pixelScale; }

    void setRasterType(RasterType _rasterType) { m_rasterType = _rasterType; }

    void setTexCoordsGeneration(bool _texCoordsGeneration) { m_texCoordsGeneration = _texCoordsGeneration; }

    bool genTexCoords() const { return m_texCoordsGeneration; }

    void setID(uint32_t _id) { m_id = _id; }

    Material& getMaterial() { return *m_material.material; }

    ShaderSource& getShaderSource() const { return *m_shaderSource; }

    const std::string& getName() const { return m_name; }
    const uint32_t& getID() const { return m_id; }

    virtual size_t dynamicMeshSize() const { return 0; }

    virtual bool hasRasters() const { return m_rasterType != RasterType::none; }

    std::vector<StyleUniform>& styleUniforms() { return m_mainUniforms.styleUniforms; }

    void setDefaultDrawRule(std::unique_ptr<DrawRuleData>&& _rule);
    void applyDefaultDrawRules(DrawRule& _rule) const;

    virtual std::unique_ptr<StyleBuilder> createBuilder() const = 0;

    GLenum drawMode() const { return m_drawMode; }
    float pixelScale() const { return m_pixelScale; }
    const auto& vertexLayout() const { return m_vertexLayout; }

    bool hasColorShaderBlock() const { return m_hasColorShaderBlock; }

};

}
