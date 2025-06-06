#include "xml_parser.hpp"
#include "tinyxml2.h"
#include <cmath>
#include <fstream>
#include <iostream>

using namespace tinyxml2;

// gray depth values need to be >0 (to distinguish from the background color)
unsigned char lastGroupID = 1;

std::map<unsigned char, GroupConfig*> clickableGroups;

void parseGroupsInfo(XMLElement* groupElement, GroupConfig& group)
{
    const char* nameAttr = groupElement->Attribute("name");
    if (!nameAttr)
        return;

    std::string groupName = nameAttr;

    // read group info from the file
    const char* infoFileAttr = groupElement->Attribute("clickableInfo");
    if (!infoFileAttr)
        return;

    std::ifstream groupInfoFile(infoFileAttr);
    if (!groupInfoFile.is_open()) {
        std::cerr << "Failed to open file: " << infoFileAttr << std::endl;
        return;
    }

    std::string groupInfoText, line;
    while (std::getline(groupInfoFile, line)) {
        groupInfoText += line + '\n';
    }
    groupInfoFile.close();

    group.name = groupName;
    group.infoText = groupInfoText;
    group.id = lastGroupID++;

    clickableGroups[group.id] = &group;
}

void parseGroup(XMLElement* groupElement, GroupConfig& group, std::map<std::string, Model*>& filesModels)
{
    // parse transformations if avilable
    XMLElement* transformElement = groupElement->FirstChildElement("transform");
    if (transformElement) {
        for (XMLElement* child = transformElement->FirstChildElement(); child != nullptr; child = child->NextSiblingElement()) {
            const std::string tag = child->Name();

            Transform t;

            if (tag == "translate") {
                t.type = TransformType::Translate;

                XMLElement* pointElement = child->FirstChildElement("point");

                if (pointElement) {
                    child->QueryFloatAttribute("time", &t.curveTime);
                    child->QueryBoolAttribute("align", &t.align);

                    size_t count = 0;
                    for (XMLElement* tmp = pointElement; tmp != nullptr; tmp = tmp->NextSiblingElement("point"))
                        count++;

                    t.numberCurvePoints = count;
                    t.curvePoints = new float*[count];

                    size_t i = 0;
                    for (XMLElement* tmp = pointElement; tmp != nullptr; tmp = tmp->NextSiblingElement("point")) {
                        t.curvePoints[i] = new float[3];
                        tmp->QueryFloatAttribute("x", &t.curvePoints[i][0]);
                        tmp->QueryFloatAttribute("y", &t.curvePoints[i][1]);
                        tmp->QueryFloatAttribute("z", &t.curvePoints[i][2]);
                        i++;
                    }
                } else {
                    child->QueryFloatAttribute("x", &t.x);
                    child->QueryFloatAttribute("y", &t.y);
                    child->QueryFloatAttribute("z", &t.z);
                }

            } else if (tag == "rotate") {
                t.type = TransformType::Rotate;
                child->QueryFloatAttribute("angle", &t.angle);
                child->QueryFloatAttribute("time", &t.time);
                child->QueryFloatAttribute("x", &t.x);
                child->QueryFloatAttribute("y", &t.y);
                child->QueryFloatAttribute("z", &t.z);

            } else if (tag == "scale") {
                t.type = TransformType::Scale;
                child->QueryFloatAttribute("x", &t.x);
                child->QueryFloatAttribute("y", &t.y);
                child->QueryFloatAttribute("z", &t.z);
            }

            group.transforms.push_back(t);
        }
    }

    // Parse <models> element.
    XMLElement* modelsElement = groupElement->FirstChildElement("models");
    if (modelsElement) {
        XMLElement* modelElement = modelsElement->FirstChildElement("model");
        while (modelElement) {
            if (modelElement->Attribute("file")) {
                std::string fileName = modelElement->Attribute("file");

                ModelCore* modelCore = new ModelCore();
                modelCore->file = fileName;

                Model* modelConfig = new Model();
                modelConfig->modelCore = modelCore;

                XMLElement* textureElement = modelElement->FirstChildElement("texture");
                if (textureElement) {
                    std::string textureFilePath = textureElement->Attribute("file");
                    modelConfig->textureFilePath = textureFilePath;
                }

                XMLElement* colorElement = modelElement->FirstChildElement("color");
                if (colorElement) {
                    XMLElement* diffuse = colorElement->FirstChildElement("diffuse");
                    if (diffuse) {
                        diffuse->QueryFloatAttribute("R", &modelConfig->material.diffuse[0]);
                        diffuse->QueryFloatAttribute("G", &modelConfig->material.diffuse[1]);
                        diffuse->QueryFloatAttribute("B", &modelConfig->material.diffuse[2]);
                        modelConfig->material.diffuse[3] = 1.0f;
                    }

                    XMLElement* ambient = colorElement->FirstChildElement("ambient");
                    if (ambient) {
                        ambient->QueryFloatAttribute("R", &modelConfig->material.ambient[0]);
                        ambient->QueryFloatAttribute("G", &modelConfig->material.ambient[1]);
                        ambient->QueryFloatAttribute("B", &modelConfig->material.ambient[2]);
                        modelConfig->material.ambient[3] = 1.0f;
                    }

                    XMLElement* specular = colorElement->FirstChildElement("specular");
                    if (specular) {
                        specular->QueryFloatAttribute("R", &modelConfig->material.specular[0]);
                        specular->QueryFloatAttribute("G", &modelConfig->material.specular[1]);
                        specular->QueryFloatAttribute("B", &modelConfig->material.specular[2]);
                        modelConfig->material.specular[3] = 1.0f;
                    }

                    XMLElement* emissive = colorElement->FirstChildElement("emissive");
                    if (emissive) {
                        emissive->QueryFloatAttribute("R", &modelConfig->material.emissive[0]);
                        emissive->QueryFloatAttribute("G", &modelConfig->material.emissive[1]);
                        emissive->QueryFloatAttribute("B", &modelConfig->material.emissive[2]);
                        modelConfig->material.emissive[3] = 1.0f;
                    }

                    XMLElement* shininess = colorElement->FirstChildElement("shininess");
                    if (shininess) {
                        shininess->QueryFloatAttribute("value", &modelConfig->material.shininess);
                    }

                    // converts from 0–255 to 0.0–1.0
                    for (int i = 0; i < 3; i++) {
                        modelConfig->material.diffuse[i] /= 255.0f;
                        modelConfig->material.ambient[i] /= 255.0f;
                        modelConfig->material.specular[i] /= 255.0f;
                        modelConfig->material.emissive[i] /= 255.0f;
                    }
                }

                const std::string baseKey = fileName;
                std::string chosenKey;

                // if there's no previous entry (e.g., "sphere.3d"), just insert it immediately
                auto it0 = filesModels.find(baseKey);
                if (it0 == filesModels.end()) {
                    chosenKey = baseKey;
                    filesModels[chosenKey] = modelConfig;
                } else {
                    // there's at least one entry whose key starts with "sphere.3d" (either exactly "sphere.3d" or "sphere.3d_alt",
                    // "sphere.3d_alt1", ...). scan them all to see if one matches the new modelConfig exactly
                    bool foundEqual = false;
                    for (auto const& entry : filesModels) {
                        const std::string& key = entry.first;
                        Model* otherModel = entry.second;
                        // key.starts_with(baseKey+"_alt")
                        if (key == baseKey || (key.rfind(baseKey + "_alt", 0) == 0)) {
                            bool sameCore = *(modelConfig->modelCore) == *otherModel->modelCore;
                            bool sameMat = modelConfig->material == otherModel->material;
                            bool sameTexture = modelConfig->textureFilePath == otherModel->textureFilePath;

                            if (sameCore && sameMat && sameTexture) {
                                // reuse this key
                                chosenKey = key;
                                foundEqual = true;
                                break;
                            }
                        }
                    }

                    if (!foundEqual) {
                        // no match found among the existing entries: invent a fresh "_altN"
                        int counter = 1;
                        do {
                            chosenKey = baseKey + "_alt" + std::to_string(counter++);
                        } while (filesModels.count(chosenKey));

                        filesModels[chosenKey] = modelConfig;
                    }
                }

                // record which key was used, and push into the group

                modelConfig->filesModelsKey = chosenKey;
                group.models.push_back(modelConfig);
            }

            modelElement = modelElement->NextSiblingElement("model");
        }
    }

    // recursively parse nested child groups if available
    XMLElement* childGroupElement = groupElement->FirstChildElement("group");
    while (childGroupElement) {
        GroupConfig* childGroup = new GroupConfig();
        parseGroupsInfo(childGroupElement, *childGroup);

        parseGroup(childGroupElement, *childGroup, filesModels);
        group.children.push_back(childGroup);
        childGroupElement = childGroupElement->NextSiblingElement("group");
    }
}

WorldConfig XMLParser::parseXML(const std::string& filename)
{
    clickableGroups.clear();
    lastGroupID = 1;
    WorldConfig config;
    XMLDocument doc;
    if (doc.LoadFile(filename.c_str()) != XML_SUCCESS) {
        std::cerr << "Failed to load XML file: " << filename << std::endl;
        return config;
    }

    XMLElement* world = doc.FirstChildElement("world");
    if (!world)
        return config;

    XMLElement* window = world->FirstChildElement("window");
    if (window) {
        window->QueryIntAttribute("width", &config.window.width);
        window->QueryIntAttribute("height", &config.window.height);
    }

    XMLElement* camera = world->FirstChildElement("camera");
    if (camera) {
        XMLElement* position = camera->FirstChildElement("position");
        if (position) {
            position->QueryFloatAttribute("x", &config.camera.position.x);
            position->QueryFloatAttribute("y", &config.camera.position.y);
            position->QueryFloatAttribute("z", &config.camera.position.z);
        }
        XMLElement* lookAt = camera->FirstChildElement("lookAt");
        if (lookAt) {
            lookAt->QueryFloatAttribute("x", &config.camera.lookAt.x);
            lookAt->QueryFloatAttribute("y", &config.camera.lookAt.y);
            lookAt->QueryFloatAttribute("z", &config.camera.lookAt.z);
        }
        XMLElement* up = camera->FirstChildElement("up");
        if (up) {
            up->QueryFloatAttribute("x", &config.camera.up.x);
            up->QueryFloatAttribute("y", &config.camera.up.y);
            up->QueryFloatAttribute("z", &config.camera.up.z);
        }
        XMLElement* projection = camera->FirstChildElement("projection");
        if (projection) {
            projection->QueryFloatAttribute("fov", &config.camera.projection.fov);
            projection->QueryFloatAttribute("near", &config.camera.projection.near1);
            projection->QueryFloatAttribute("far", &config.camera.projection.far1);
        }
    }

    // Parse lights
    XMLElement* lightsElement = world->FirstChildElement("lights");
    if (lightsElement) {
        for (XMLElement* lightEl = lightsElement->FirstChildElement("light");
            lightEl != nullptr;
            lightEl = lightEl->NextSiblingElement("light")) {

            LightConfig light;
            const char* type = lightEl->Attribute("type");

            if (strcmp(type, "point") == 0) {
                light.type = LightType::POINT;
                lightEl->QueryFloatAttribute("posX", &light.position[0]);
                lightEl->QueryFloatAttribute("posY", &light.position[1]);
                lightEl->QueryFloatAttribute("posZ", &light.position[2]);
                light.position[3] = 1.0f;
            } else if (strcmp(type, "directional") == 0) {
                light.type = LightType::DIRECTIONAL;
                lightEl->QueryFloatAttribute("dirX", &light.position[0]);
                lightEl->QueryFloatAttribute("dirY", &light.position[1]);
                lightEl->QueryFloatAttribute("dirZ", &light.position[2]);
                light.position[3] = 0.0f;
            } else if (strcmp(type, "spotlight") == 0) {
                light.type = LightType::SPOTLIGHT;
                lightEl->QueryFloatAttribute("posX", &light.position[0]);
                lightEl->QueryFloatAttribute("posY", &light.position[1]);
                lightEl->QueryFloatAttribute("posZ", &light.position[2]);
                light.position[3] = 1.0f;

                lightEl->QueryFloatAttribute("dirX", &light.direction[0]);
                lightEl->QueryFloatAttribute("dirY", &light.direction[1]);
                lightEl->QueryFloatAttribute("dirZ", &light.direction[2]);

                lightEl->QueryFloatAttribute("cutoff", &light.cutoff);
            }

            config.lights.push_back(light);
        }
    }

    XMLElement* group = world->FirstChildElement("group");
    if (group) {
        parseGroupsInfo(group, config.group);
        parseGroup(group, config.group, config.filesModels);
    }

    config.clickableGroups = clickableGroups;

    return config;
}

void calculateSphericalCoordinates(float x, float y, float z, float& alpha, float& beta, float& radius)
{
    radius = std::sqrt(x * x + y * y + z * z);
    alpha = std::atan2(z, x);
    beta = std::asin(y / radius);
}

void XMLParser::configureFromXML(WorldConfig& config)
{
    std::cout << "World configuration:" << std::endl;
    std::cout << "Window width: " << config.window.width << ", height: " << config.window.height << std::endl;
    std::cout << "Camera position: (" << config.camera.position.x << ", " << config.camera.position.y << ", " << config.camera.position.z << ")" << std::endl;
    std::cout << "Camera lookAt: (" << config.camera.lookAt.x << ", " << config.camera.lookAt.y << ", " << config.camera.lookAt.z << ")" << std::endl;
    std::cout << "Camera up: (" << config.camera.up.x << ", " << config.camera.up.y << ", " << config.camera.up.z << ")" << std::endl;
    std::cout << "Camera projection: fov=" << config.camera.projection.fov << ", near=" << config.camera.projection.near1 << ", far=" << config.camera.projection.far1 << std::endl;

    float alpha, beta, radius;
    calculateSphericalCoordinates(config.camera.position.x, config.camera.position.y, config.camera.position.z,
        alpha, beta, radius);

    config.camera.cameraAngle = alpha;
    config.camera.cameraAngleY = beta;
    config.camera.cameraDistance = radius;

    GroupConfig rootGroup = config.group;
}
