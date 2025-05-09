#include "scene/importer.h"

#include "log.h"
#include "platform.h"
#include "util/asyncWorker.h"
#include "util/yamlUtil.h"
#include "util/zipArchive.h"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <condition_variable>

using YAML::Node;
using YAML::NodeType;

namespace Tangram {

Importer::Importer() {}
Importer::~Importer() {}

Node Importer::loadSceneData(Platform& _platform, const Url& _sceneUrl, const std::string& _sceneYaml) {

    Url nextUrlToImport;

    if (!_sceneYaml.empty()) {
        // Load scene from yaml string.
        addSceneYaml(_sceneUrl, _sceneYaml.data(), _sceneYaml.length());
    } else {
        // Load scene from yaml file.
        m_sceneQueue.push_back(_sceneUrl);
    }
    
    std::atomic_uint activeDownloads(0);
    std::condition_variable condition;

    while (true) {
        {
            std::unique_lock<std::mutex> lock(m_sceneMutex);

            if (m_sceneQueue.empty() || m_canceled) {
                if (activeDownloads == 0) {
                    break;
                }
                condition.wait(lock);
            }
            
            if (m_sceneQueue.empty() || m_canceled) {
                continue;
            }

            nextUrlToImport = m_sceneQueue.back();
            m_sceneQueue.pop_back();

            // Mark Url as going-to-be-imported to prevent duplicate work.
            m_sceneNodes.emplace(nextUrlToImport, SceneNode{});
        }

        auto cb = [&, nextUrlToImport](UrlResponse&& response) {
            std::unique_lock<std::mutex> lock(m_sceneMutex);
            if (response.error) {
                LOGE("Unable to retrieve '%s': %s", nextUrlToImport.string().c_str(),
                     response.error);
            } else {
                LOGD("skyway nextUrlToImport = %s",nextUrlToImport.data().c_str());
                addSceneData(nextUrlToImport, std::move(response.content));
            }
            activeDownloads--;
            condition.notify_one();
        };
        
        activeDownloads++;

        if (nextUrlToImport.scheme() == "zip") {
            readFromZip(nextUrlToImport, cb);
        } else {
            auto handle = _platform.startUrlRequest(nextUrlToImport, cb);
            std::unique_lock<std::mutex> lock(m_sceneMutex);
            m_urlRequests.push_back(handle);
        }
    }

    if (m_canceled) { return Node(); }

    LOGD("Processing scene import Stack:");
    std::unordered_set<Url> imported;
    Node root;
    importScenesRecursive(root, _sceneUrl, imported);

    // After merging all scenes, resolve texture nodes as named textures or URLs.
    Node textures = root["textures"];
    for (auto& sceneNode : m_sceneNodes) {
        auto sceneUrl = sceneNode.first;
        if (isZipArchiveUrl(sceneUrl)) {
            sceneUrl = getBaseUrlForZipArchive(sceneUrl);
        }
        for (auto& node : sceneNode.second.pendingUrlNodes) {
            // If the node does not contain a named texture in the final scene, treat it as a URL relative to the scene
            // file where it was originally encountered.
            if (!textures[node.Scalar()]) {
                node = sceneUrl.resolve(Url(node.Scalar())).string();
            }
        }
    }

    m_sceneNodes.clear();

    return root;
}

void Importer::cancelLoading(Platform& _platform) {
    std::unique_lock<std::mutex> lock(m_sceneMutex);
    m_canceled = true;
    for (auto handle : m_urlRequests) {
        _platform.cancelUrlRequest(handle);
    }
}

void Importer::addSceneData(const Url& sceneUrl, std::vector<char>&& sceneData) {
    LOGD("Process: '%s'", sceneUrl.string().c_str());

    if (!isZipArchiveUrl(sceneUrl)) {
        addSceneYaml(sceneUrl, sceneData.data(), sceneData.size());
        return;
    }

    // We're loading a scene from a zip archive
    // First, create an archive from the data.
    auto zipArchive = std::make_shared<ZipArchive>();
    zipArchive->loadFromMemory(std::move(sceneData));

    // Find the "base" scene file in the archive entries.
    for (const auto& entry : zipArchive->entries()) {
        auto ext = Url::getPathExtension(entry.path);
        // The "base" scene file must have extension "yaml" or "yml" and be
        // at the root directory of the archive (i.e. no '/' in path).
        if ((ext == "yaml" || ext == "yml") && entry.path.find('/') == std::string::npos) {
            // Found the base, now extract the contents to the scene string.
            std::vector<char> yaml;
            yaml.resize(entry.uncompressedSize);

            zipArchive->decompressEntry(&entry, &yaml[0]);

            addSceneYaml(sceneUrl, yaml.data(), yaml.size());
            break;
        }
    }

    m_zipArchives.emplace(sceneUrl, zipArchive);
}

UrlRequestHandle Importer::readFromZip(const Url& url, UrlCallback callback) {

    if (!m_zipWorker) {
        m_zipWorker = std::make_unique<AsyncWorker>();
        m_zipWorker->waitForCompletion();
    }

    m_zipWorker->enqueue([=](){
        UrlResponse response;
        // URL for a file in a zip archive, get the encoded source URL.
        auto source = Importer::getArchiveUrlForZipEntry(url);
        // Search for the source URL in our archive map.
        auto it = m_zipArchives.find(source);
        if (it != m_zipArchives.end()) {
            auto& archive = it->second;
            // Found the archive! Now create a response for the request.
            auto zipEntryPath = url.path().substr(1);
            auto entry = archive->findEntry(zipEntryPath);
            if (entry) {
                response.content.resize(entry->uncompressedSize);
                bool success = archive->decompressEntry(entry, response.content.data());
                if (!success) {
                    response.error = "Unable to decompress zip archive file.";
                }
            } else {
                response.error = "Did not find zip archive entry.";
            }
        } else {
            response.error = "Could not find zip archive.";
        }
        callback(std::move(response));
    });
    return 0;
}

void Importer::addSceneYaml(const Url& sceneUrl, const char* sceneYaml, size_t length) {

    auto& sceneNode = m_sceneNodes[sceneUrl];

    try {
        sceneNode.yaml = YamlUtil::loadNoCopy(sceneYaml, length);
    } catch (const YAML::ParserException& e) {
        LOGE("Parsing scene config '%s'", e.what());
        return;
    }

    if (!sceneNode.yaml.IsDefined() || !sceneNode.yaml.IsMap()) {
        LOGE("Scene is not a valid YAML map: %s", sceneUrl.string().c_str());
        return;
    }

    sceneNode.imports = getResolvedImportUrls(sceneNode.yaml, sceneUrl);

    sceneNode.pendingUrlNodes = getTextureUrlNodes(sceneNode.yaml);

    // Remove 'import' values so they don't get merged.
    sceneNode.yaml.remove("import");

    for (const auto& url : sceneNode.imports) {
        // Check if this scene URL has been (or is going to be) imported already
        if (m_sceneNodes.find(url) == m_sceneNodes.end()) {
            m_sceneQueue.push_back(url);
        }
    }
}

std::vector<Url> Importer::getResolvedImportUrls(const Node& sceneNode, const Url& baseUrl) {

    std::vector<Url> sceneUrls;

    auto base = baseUrl;
    if (isZipArchiveUrl(baseUrl)) {
        base = getBaseUrlForZipArchive(baseUrl);
    }

    if (sceneNode.IsMap()) {
        if (const Node& import = sceneNode["import"]) {
            if (import.IsScalar()) {
                sceneUrls.push_back(base.resolve(Url(import.Scalar())));
            } else if (import.IsSequence()) {
                for (const auto &path : import) {
                    if (path.IsScalar()) {
                        sceneUrls.push_back(base.resolve(Url(path.Scalar())));
                    }
                }
            }
        }
    }

    return sceneUrls;
}

void Importer::importScenesRecursive(Node& root, const Url& sceneUrl, std::unordered_set<Url>& imported) {

    LOGD("Starting importing Scene: %s", sceneUrl.string().c_str());

    // Insert self to handle self-imports cycles
    imported.insert(sceneUrl);

    auto& sceneNode = m_sceneNodes[sceneUrl];

    // If an import URL is already in the imported set that means it is imported by a "parent" scene file to this one.
    // The parent import will assign the same values, so we can safely skip importing it here. This saves some work and
    // also prevents import cycles.
    //
    // It is important that we don't merge the same YAML node more than once. YAML node assignment is by reference, so
    // merging mutates the original input nodes.
    auto it = std::remove_if(sceneNode.imports.begin(), sceneNode.imports.end(),
                             [&](auto& i){ return imported.find(i) != imported.end(); });

    if (it != sceneNode.imports.end()) {
        LOGD("Skipping redundant import(s)");
        sceneNode.imports.erase(it, sceneNode.imports.end());
    }

    imported.insert(sceneNode.imports.begin(), sceneNode.imports.end());

    for (const auto& url : sceneNode.imports) {
        importScenesRecursive(root, url, imported);
    }

    mergeMapFields(root, sceneNode.yaml);

    resolveSceneUrls(root, sceneUrl);
}

void Importer::mergeMapFields(Node& target, const Node& import) {
    if (!target.IsMap() || !import.IsMap()) {

        if (target.IsDefined() && !target.IsNull() && (target.Type() != import.Type())) {
            LOGN("Merging different node types: \n'%s'\n<--\n'%s'",
                 Dump(target).c_str(), Dump(import).c_str());
        }

        target = import;

    } else {
        for (const auto& entry : import) {
            const auto& key = entry.first.Scalar();
            const auto& source = entry.second;
            auto dest = target[key];
            mergeMapFields(dest, source);
        }
    }
}

bool Importer::isZipArchiveUrl(const Url& url) {
    return Url::getPathExtension(url.path()) == "zip";
}

Url Importer::getBaseUrlForZipArchive(const Url& archiveUrl) {
    auto encodedSourceUrl = Url::escapeReservedCharacters(archiveUrl.string());
    auto baseUrl = Url("zip://" + encodedSourceUrl);
    return baseUrl;
}

Url Importer::getArchiveUrlForZipEntry(const Url& zipEntryUrl) {
    auto encodedSourceUrl = zipEntryUrl.netLocation();
    auto source = Url(Url::unEscapeReservedCharacters(encodedSourceUrl));
    return source;
}

bool nodeIsPotentialUrl(const Node& node) {
    // Check that the node is scalar and not null.
    if (!node || !node.IsScalar()) { return false; }

    // Check that the node does not contain a 'global' reference.
    if (node.Scalar().compare(0, 7, "global.") == 0) { return false; }

    return true;
}

bool nodeIsPotentialTextureUrl(const Node& node) {

    if (!nodeIsPotentialUrl(node)) { return false; }

    // Check that the node is not a number or a boolean.
    bool booleanValue = false;
    double numberValue = 0.;
    if (YAML::convert<bool>::decode(node, booleanValue)) { return false; }
    if (YAML::convert<double>::decode(node, numberValue)) { return false; }

    return true;
}

std::vector<Node> Importer::getTextureUrlNodes(Node& root) {

    std::vector<Node> nodes;

    if (Node styles = root["styles"]) {

        for (auto entry : styles) {

            Node style = entry.second;
            if (!style.IsMap()) { continue; }

            //style->texture
            if (Node texture = style["texture"]) {
                if (nodeIsPotentialTextureUrl(texture)) {
                    nodes.push_back(texture);
                }
            }

            //style->material->texture
            if (Node material = style["material"]) {
                if (!material.IsMap()) { continue; }
                for (auto& prop : {"emission", "ambient", "diffuse", "specular", "normal"}) {
                    if (Node propNode = material[prop]) {
                        if (!propNode.IsMap()) { continue; }
                        if (Node matTexture = propNode["texture"]) {
                            if (nodeIsPotentialTextureUrl(matTexture)) {
                                nodes.push_back(matTexture);
                            }
                        }
                    }
                }
            }

            //style->shader->uniforms->texture
            if (Node shaders = style["shaders"]) {
                if (!shaders.IsMap()) { continue; }
                if (Node uniforms = shaders["uniforms"]) {
                    for (auto uniformEntry : uniforms) {
                        Node uniformValue = uniformEntry.second;
                        if (nodeIsPotentialTextureUrl(uniformValue)) {
                            nodes.push_back(uniformValue);
                        } else if (uniformValue.IsSequence()) {
                            for (Node u : uniformValue) {
                                if (nodeIsPotentialTextureUrl(u)) {
                                    nodes.push_back(u);
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    return nodes;
}

void Importer::resolveSceneUrls(Node& root, const Url& baseUrl) {

    auto base = baseUrl;
    if (isZipArchiveUrl(baseUrl)) {
        base = getBaseUrlForZipArchive(baseUrl);
    }

    // Resolve global texture URLs.

    Node textures = root["textures"];

    if (textures.IsDefined()) {
        for (auto texture : textures) {
            if (Node textureUrlNode = texture.second["url"]) {
                if (nodeIsPotentialUrl(textureUrlNode)) {
                    textureUrlNode = base.resolve(Url(textureUrlNode.Scalar())).string();
                }
            }
        }
    }

    // Resolve data source URLs.

    if (Node sources = root["sources"]) {
        for (auto source : sources) {
            if (!source.second.IsMap()) { continue; }
            if (Node sourceUrl = source.second["url"]) {
                if (nodeIsPotentialUrl(sourceUrl)) {
                    sourceUrl = base.resolve(Url(sourceUrl.Scalar())).string();
                }
            }
        }
    }

    // Resolve font URLs.

    if (Node fonts = root["fonts"]) {
        if (fonts.IsMap()) {
            for (const auto& font : fonts) {
                if (font.second.IsMap()) {
                    auto urlNode = font.second["url"];
                    if (nodeIsPotentialUrl(urlNode)) {
                        urlNode = base.resolve(Url(urlNode.Scalar())).string();
                    }
                } else if (font.second.IsSequence()) {
                    for (auto& fontNode : font.second) {
                        auto urlNode = fontNode["url"];
                        if (nodeIsPotentialUrl(urlNode)) {
                            urlNode = base.resolve(Url(urlNode.Scalar())).string();
                        }
                    }
                }
            }
        }
    }
}

}
