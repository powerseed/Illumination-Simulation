#ifndef raytracer_h
#define raytracer_h
#include <glm/glm.hpp>
#include <string>
#include <glm/gtx/string_cast.hpp>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include "json.hpp"
using json = nlohmann::json;

struct shape
{
	std::string type;
	glm::vec3 position;
	glm::vec3 normal;
	float radius;
	float half_height;
	glm::vec3 material_ambient;
	glm::vec3 material_diffuse;
	glm::vec3 material_specular;
	float material_shininess;
	float material_roughness;
	glm::vec3 material_reflective;
	glm::vec3 material_transmissive;
	float material_refraction;
	std::vector<float> bounding;
	glm::vec4 vertex0;
	glm::vec4 vertex1;
	glm::vec4 vertex2;
	shape * sub_shape1;
	shape * sub_shape2;
};

struct Node
{
	std::vector<float> bounding;
	std::vector<Node *> children;
	std::vector<shape *> shapes_contained;
};

typedef glm::vec3 point3;
typedef glm::vec3 colour3;

extern double fov;
extern colour3 background_colour;
extern Node * ocTree_root;

void choose_scene(char const *fn);

bool trace(point3 &e, point3 &s, colour3 &colour);

bool shadowTesting(const point3 &e, const point3 &s, int type);

void mirrorReflection(point3 &e, point3 &s, colour3 &colour, int depth,
	glm::vec3 &material_ambient, glm::vec3 &material_diffuse, glm::vec3 &material_specular,
	float &material_shininess, glm::vec3 &material_reflective, glm::vec3 &material_transmissive, float &material_refraction, float &material_roughness,
	glm::vec3 &N, glm::vec3 lastPoint);

bool hitTesting(const point3 &e, const point3 &s,
	glm::vec3 &material_ambient, glm::vec3 &material_diffuse, glm::vec3 &material_specular,
	float &material_shininess, glm::vec3 &material_reflective, glm::vec3 &material_transmissive, float &material_refraction, float &material_roughness,
	glm::vec3 &intersection, glm::vec3 &N, glm::vec3 &center, int &type, float &radius, std::vector<shape *> objects_to_for_hit_testing);

void getColor(colour3 &colour,
	glm::vec3 &material_ambient, glm::vec3 &material_diffuse, glm::vec3 &material_specular,
	float &material_shininess, glm::vec3 &material_reflective, glm::vec3 &material_transmissive, float &material_refraction, float &material_roughness,
	const glm::vec3 &intersection, glm::vec3 &N, glm::vec3 &V);

void getBoundingAndShapeList();
void partition(Node * parent, std::vector<shape *> listOfShapes, std::vector<float> bounding);
void ray_box_intersection(glm::vec3 &e, glm::vec3 &s, Node * node, std::vector<shape *> &objects_to_for_hit_testing, bool pick);

#endif
