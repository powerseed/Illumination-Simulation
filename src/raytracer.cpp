#include "raytracer.h"
#include <stdio.h>
#include <stdlib.h>
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>

using json = nlohmann::json;
float refractionOfAir = 1.0002926f;
const char *PATH = "scenes/";

json scene;

int point = 1;
int direction = 2;
int spot = 3;
int sphere = 4;
int triangle = 5;
int plane = 6;

double fov = 60;
colour3 background_colour(0, 0, 0);
Node * ocTree_root;

glm::vec3 light_ambient_color;

std::vector<glm::vec3> light_directional_color;
std::vector<glm::vec3> light_directional_direction;

std::vector<glm::vec3> light_point_color;
std::vector<glm::vec3> light_point_position;

std::vector<glm::vec3> light_spot_color;
std::vector<glm::vec3> light_spot_position;
std::vector<glm::vec3> light_spot_direction;
std::vector<float> light_spot_cutoff;

std::vector<float> bounding;

std::vector<shape *> listOfShapes;
std::vector<shape *> listOfPlanes;

glm::vec3 eye(0.0f, 0.0f, 0.0f);

json find(json &j, const std::string key, const std::string value) {
	json::iterator it;
	for (it = j.begin(); it != j.end(); ++it) {
		if (it->find(key) != it->end()) {
			if ((*it)[key] == value) {
				return *it;
			}
		}
	}
	return json();
}

glm::vec3 vector_to_vec3(const std::vector<float> &v) {
	return glm::vec3(v[0], v[1], v[2]);
}

void choose_scene(char const *fn) 
{
	if (fn == NULL) {
		std::cout << "Using default input file " << PATH << "c.json\n";
		fn = "c";
	}
	
	std::string fname = PATH + std::string(fn) + ".json";
	std::fstream in(fname);
	if (!in.is_open()) {
		std::cout << "Unable to open scene file " << fname << std::endl;
		exit(EXIT_FAILURE);
	}
	
	in >> scene;
	
	json camera = scene["camera"];

	// these are optional parameters (otherwise they default to the values initialized earlier)
	if (camera.find("field") != camera.end()) {
		fov = camera["field"];
		std::cout << "Setting fov to " << fov << " degrees.\n";
	}

	if (camera.find("background") != camera.end()) {
		background_colour = vector_to_vec3(camera["background"]);
		std::cout << "Setting background colour to " << glm::to_string(background_colour) << std::endl;
	}

	json lights = scene["lights"];

	for (json::iterator it = lights.begin(); it != lights.end(); ++it)
	{
		json &light = *it;

		if (light["type"] == "ambient")
		{
			std::vector<float> color = light["color"];
			light_ambient_color = (vector_to_vec3(color));
		}
		else if (light["type"] == "directional")
		{
			std::vector<float> color = light["color"];
			light_directional_color.push_back(vector_to_vec3(color));

			std::vector<float> direction = light["direction"];
			light_directional_direction.push_back(vector_to_vec3(direction));
		}
		else if (light["type"] == "point")
		{
			std::vector<float> color = light["color"];
			light_point_color.push_back(vector_to_vec3(color));

			std::vector<float> position = light["position"];
			light_point_position.push_back(vector_to_vec3(position));
		}
		else if (light["type"] == "spot")
		{
			std::vector<float> color = light["color"];
			light_spot_color.push_back(vector_to_vec3(color));

			std::vector<float> position = light["position"];
			light_spot_position.push_back(vector_to_vec3(position));

			std::vector<float> direction = light["direction"];
			light_spot_direction.push_back(vector_to_vec3(direction));

			float cutoff = light["cutoff"];
			light_spot_cutoff.push_back(cutoff);
		}
		else if (light["type"] == "area")
		{
			std::vector<float> color = light["color"];
			glm::vec3 vector_color = vector_to_vec3(color);

			std::vector<float> start = light["start"];
			std::vector<float> end = light["end"];

			float num_in_x = (end.at(0) - start.at(0)) / 0.1f;
			float num_in_z = (start.at(2) - end.at(2)) / 0.1f;
			std::cout << num_in_x << " " << num_in_z << std::endl;
			for (float x = start.at(0); x <= end.at(0); x += 0.1f)
			{
				for (float z = start.at(2); z >= end.at(2); z -= 0.1f)
				{
					light_point_color.push_back( vector_color / (num_in_x * num_in_z) );
					light_point_position.push_back( glm::vec3(x, start.at(1), z) );
				}
			}
		}
	}
}

glm::vec3 refract(const glm::vec3 &I, const glm::vec3 &N, const float &ior)
{
	float cosi = glm::clamp( dot(I, N), -1.0f, 1.0f);
	float etai = refractionOfAir;
	float etat = ior;
	glm::vec3 n = N;

	if (cosi < 0)
	{
		cosi = -cosi; 
	}
	else
	{ 
		std::swap(etai, etat);
		n = -N; 
	}

	float eta = etai / etat;
	float k = 1 - eta * eta * (1 - cosi * cosi);

	if (k < 0)
		return glm::vec3 (0, 0, 0);
	else
	{
		return eta * I + (eta * cosi - sqrtf(k)) * n;
	}
}

bool trace(point3 &e, point3 &s, colour3 &colour)
{
	bool isHit = false;

	glm::vec3 material_ambient = glm::vec3(0, 0, 0);
	glm::vec3 material_diffuse = glm::vec3(0, 0, 0);
	glm::vec3 material_specular = glm::vec3(0, 0, 0);
	float material_shininess = 0;
	float material_roughness = 0;
	glm::vec3 material_reflective = glm::vec3(0, 0, 0);
	glm::vec3 material_transmissive = glm::vec3(0, 0, 0);
	float material_refraction = refractionOfAir;
	glm::vec3 intersection = glm::vec3(0, 0, 0);
	glm::vec3 N = glm::vec3(0, 0, 0);
	glm::vec3 V = normalize(e - s);
	glm::vec3 c = glm::vec3(0, 0, 0);
	int type = -1;
	float radius = 0;

	std::vector<shape *> objects_to_for_hit_testing_one;
	ray_box_intersection(e, s, ocTree_root, objects_to_for_hit_testing_one, false);

	isHit = hitTesting(e, s,
					   material_ambient, material_diffuse, material_specular,
					   material_shininess, material_reflective, material_transmissive, material_refraction, material_roughness,
					   intersection, N, c, type, radius, objects_to_for_hit_testing_one);

	// eye is not at the origin.
	if (isHit && (eye.x != 0.0f || eye.y != 0.0f || eye.z != 0.0f))
	{
		glm::vec3 move_of_eye = eye - glm::vec3(0, 0, 0);
		glm::vec3 move_of_s =	distance(intersection, s) / distance(intersection, e) * move_of_eye;
		s = s + move_of_s;
		e = eye;
		V = normalize(e - s);
	}
	//
	if (isHit)
	{
		// Schlick's approximation
		if ( material_transmissive.x != 0.0f || material_transmissive.y != 0.0f || material_transmissive.z != 0.0f )
		{
			float R0 = pow((refractionOfAir - material_refraction) / (refractionOfAir + material_refraction), 2);
			float R_theta = R0 + (1 - R0) * pow((1 - dot(N, V)), 5);
			material_reflective.x = R_theta;
			material_reflective.y = R_theta;
			material_reflective.z = R_theta;
			material_transmissive.x = 1.0 - R_theta;
			material_transmissive.y = 1.0 - R_theta;
			material_transmissive.z = 1.0 - R_theta;
		}

		// mirror reflection
		glm::vec3 colourForMirror = glm::vec3(0, 0, 0);
		glm::vec3 RforMirror = normalize(2 * glm::max(dot(N, V), 0.0f) * N - V);

		glm::vec3 material_ambient_ForHitPoint = glm::vec3(0, 0, 0);
		glm::vec3 material_diffuse_ForHitPoint = glm::vec3(0, 0, 0);
		glm::vec3 material_specular_ForHitPoint = glm::vec3(0, 0, 0);
		float material_shininess_ForHitPoint = 0;
		float material_roughness_ForHitPoint = 0;
		glm::vec3 material_reflective_ForHitPoint = glm::vec3(0, 0, 0);
		glm::vec3 material_transmissive_ForHitPoint = glm::vec3(0, 0, 0);
		float material_refraction_ForHitPoint = refractionOfAir;
		glm::vec3 intersection_ForHitPoint = glm::vec3(0, 0, 0);
		glm::vec3 N_ForHitPoint = glm::vec3(0, 0, 0);
		glm::vec3 c_ForHitPoint = glm::vec3(0, 0, 0);
		int type_ForHitPoint = -1;
		float radius_ForHitPoint = -1;

		std::vector<shape *> objects_to_for_hit_testing_two;
		ray_box_intersection(intersection, RforMirror + intersection, ocTree_root, objects_to_for_hit_testing_two, false);

		if (hitTesting(intersection, RforMirror + intersection,
			material_ambient_ForHitPoint, material_diffuse_ForHitPoint, material_specular_ForHitPoint,
			material_shininess_ForHitPoint, material_reflective_ForHitPoint, material_transmissive_ForHitPoint, material_refraction_ForHitPoint, material_roughness_ForHitPoint,
			intersection_ForHitPoint, N_ForHitPoint, c_ForHitPoint, type_ForHitPoint, radius_ForHitPoint, objects_to_for_hit_testing_two)
			&&
			(material_reflective.x != 0.0f || material_reflective.y != 0.0f || material_reflective.z != 0.0f))
		{
			mirrorReflection(intersection, RforMirror + intersection, colourForMirror, 3,
				material_ambient, material_diffuse, material_specular,
				material_shininess, material_reflective, material_transmissive, material_refraction, material_roughness,
				N, e);
			colour = colourForMirror + colour;
		}
		else
		{
			getColor(colour,
				material_ambient, material_diffuse, material_specular,
				material_shininess, material_reflective, material_transmissive, material_refraction, material_roughness,
				intersection, N, V);
		}

		// transpency
		glm::vec3 I2 = glm::vec3(0, 0, 0);

		if (material_transmissive.x != 0.0f || material_transmissive.y != 0.0f || material_transmissive.z != 0.0f)
		{
			if (type == 4)//sphere
			{
				glm::vec3 Vr = normalize(refract(normalize(intersection - e), N, material_refraction));
				float determineForSecondSurface = glm::pow(dot(Vr, (intersection - c)), 2) - dot(Vr, Vr) * (dot((intersection - c), (intersection - c)) - radius * radius);

				if (determineForSecondSurface >= 0) // there is intersetion 
				{
					float rooted_determineForSecondSurface = glm::sqrt(determineForSecondSurface);
					float tForSecondSurface = (-1 * dot(Vr, (intersection - c)) + rooted_determineForSecondSurface) / dot(Vr, Vr);

					glm::vec3 positionOfSecondIntersection = intersection + tForSecondSurface * Vr;

					glm::vec3 VrForSecondSurface = normalize(refract(Vr, normalize(positionOfSecondIntersection - c), material_refraction));

					glm::vec3 material_ambient_ForSecondSurface = glm::vec3(0, 0, 0);
					glm::vec3 material_diffuse_ForSecondSurface = glm::vec3(0, 0, 0);
					glm::vec3 material_specular_ForSecondSurface = glm::vec3(0, 0, 0);
					float material_shininess_ForSecondSurface = 0;
					float material_roughness_ForSecondSurface = 0;
					glm::vec3 material_reflective_ForSecondSurface = glm::vec3(0, 0, 0);
					glm::vec3 material_transmissive_ForSecondSurface = glm::vec3(0, 0, 0);
					float material_refraction_ForSecondSurface = refractionOfAir;
					glm::vec3 intersection_ForSecondSurface = glm::vec3(0, 0, 0);
					glm::vec3 N_ForSecondSurface = glm::vec3(0, 0, 0);
					glm::vec3 c_ForSecondSurface = glm::vec3(0, 0, 0);
					int type_ForSecondSurface = -1;
					float radius_ForSecondSurface = -1;

					std::vector<shape *> objects_to_for_hit_testing_three;
					ray_box_intersection(positionOfSecondIntersection, VrForSecondSurface + positionOfSecondIntersection, ocTree_root, objects_to_for_hit_testing_three, false);

					bool isHitOther = hitTesting(positionOfSecondIntersection, VrForSecondSurface + positionOfSecondIntersection,
						material_ambient_ForSecondSurface, material_diffuse_ForSecondSurface, material_specular_ForSecondSurface,
						material_shininess_ForSecondSurface, material_reflective_ForSecondSurface, material_transmissive_ForSecondSurface, material_refraction_ForSecondSurface, material_roughness_ForSecondSurface,
						intersection_ForSecondSurface, N_ForSecondSurface, c_ForSecondSurface, type_ForSecondSurface, radius_ForSecondSurface, objects_to_for_hit_testing_three);

					if (!isHitOther)
					{
						I2 = background_colour;
					}
					else
					{
						getColor(I2,
							material_ambient_ForSecondSurface, material_diffuse_ForSecondSurface, material_specular_ForSecondSurface,
							material_shininess_ForSecondSurface, material_reflective_ForSecondSurface, material_transmissive_ForSecondSurface, material_refraction_ForSecondSurface, material_roughness_ForSecondSurface,
							intersection_ForSecondSurface, N_ForSecondSurface, -1.0f * VrForSecondSurface);
					}
					colour = (glm::vec3(1.0f, 1.0f, 1.0f) - material_transmissive) * colour + material_transmissive * I2;
				}
			}
			else
			{
				glm::vec3 Vr = normalize(refract(normalize(intersection - e), N, material_refraction));

				glm::vec3 material_ambient_ForSecondSurface = glm::vec3(0, 0, 0);
				glm::vec3 material_diffuse_ForSecondSurface = glm::vec3(0, 0, 0);
				glm::vec3 material_specular_ForSecondSurface = glm::vec3(0, 0, 0);
				float material_shininess_ForSecondSurface = 0;
				float material_roughness_ForSecondSurface = 0;
				glm::vec3 material_reflective_ForSecondSurface = glm::vec3(0, 0, 0);
				glm::vec3 material_transmissive_ForSecondSurface = glm::vec3(0, 0, 0);
				float material_refraction_ForSecondSurface = refractionOfAir;
				glm::vec3 intersection_ForSecondSurface = glm::vec3(0, 0, 0);
				glm::vec3 N_ForSecondSurface = glm::vec3(0, 0, 0);
				glm::vec3 c_ForSecondSurface = glm::vec3(0, 0, 0);
				int type_ForSecondSurface = -1;
				float radius_ForSecondSurface = -1;

				std::vector<shape *> objects_to_for_hit_testing_four;
				ray_box_intersection(intersection, Vr + intersection, ocTree_root, objects_to_for_hit_testing_four, false);

				bool isHitOther = hitTesting(intersection, Vr + intersection,
											material_ambient_ForSecondSurface, material_diffuse_ForSecondSurface, material_specular_ForSecondSurface,
											material_shininess_ForSecondSurface, material_reflective_ForSecondSurface, material_transmissive_ForSecondSurface, material_refraction_ForSecondSurface, material_roughness_ForSecondSurface,
											intersection_ForSecondSurface, N_ForSecondSurface, c_ForSecondSurface, type_ForSecondSurface, radius_ForSecondSurface, objects_to_for_hit_testing_four);

				if (!isHitOther)
				{
					I2 = background_colour;
				}
				else
				{
					getColor(I2,
						material_ambient_ForSecondSurface, material_diffuse_ForSecondSurface, material_specular_ForSecondSurface,
						material_shininess_ForSecondSurface, material_reflective_ForSecondSurface, material_transmissive_ForSecondSurface, material_refraction_ForSecondSurface, material_roughness_ForSecondSurface,
						intersection_ForSecondSurface, N_ForSecondSurface, -1.0f * Vr);
				}
				colour = (glm::vec3(1.0f, 1.0f, 1.0f) - material_transmissive) * colour + material_transmissive * I2;
			}
		}
	}
	return isHit;
}

bool shadowTesting(const point3 &e, const point3 &s, int type)
{
	for (int i = 0; i < listOfShapes.size(); i++)
	{
		if (listOfShapes.at(i)->type == "sphere")
		{
			glm::vec3 c = listOfShapes.at(i)->position;
			float radius = listOfShapes.at(i)->radius;

			glm::vec3 d = (s - e);

			float determine = glm::pow(dot(d, (e - c)), 2) - dot(d, d) * (dot((e - c), (e - c)) - radius * radius);

			if (determine >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine);
				float t = (-1 * dot(d, (e - c)) - rooted_determine) / dot(d, d);

				if (type == 1 || type == 3)	// point or spot
				{
					if ( t > 0.001 && t < 1 )
					{
						return true;
					}
				}
				else if (type == 2)	// direction
				{
					if (t > 0.001)
					{
						return true;
					}
				}
			}
		}// if
		else if (listOfShapes.at(i)->type == "triangle")
		{
			glm::vec3 vertex0_vector = listOfShapes.at(i)->vertex0;
			glm::vec3 vertex1_vector = listOfShapes.at(i)->vertex1;
			glm::vec3 vertex2_vector = listOfShapes.at(i)->vertex2;

			glm::vec3 n = normalize(cross(vertex1_vector - vertex0_vector, vertex2_vector - vertex1_vector));
			glm::vec3 d = s - e;

			float denominator = dot(n, d);

			if (denominator != 0)
			{
				float t = dot(n, (vertex0_vector - e)) / denominator;

				if (type == 1 || type == 3)	// point or spot
				{
					if (t > 0.001 && t < 1)
					{
						glm::vec3 intersection = e + t * d;

						glm::vec3 b_a = vertex1_vector - vertex0_vector;
						glm::vec3 x_a = intersection - vertex0_vector;

						glm::vec3 c_b = vertex2_vector - vertex1_vector;
						glm::vec3 x_b = intersection - vertex1_vector;

						glm::vec3 a_c = vertex0_vector - vertex2_vector;
						glm::vec3 x_c = intersection - vertex2_vector;

						bool sign1 = dot(cross(b_a, x_a), n) > 0;
						bool sign2 = dot(cross(c_b, x_b), n) > 0;
						bool sign3 = dot(cross(a_c, x_c), n) > 0;

						if (sign1 && sign2 && sign3) // intersect with this triangle
						{
							return true;
						}
					}
				}
				else if (type == 2)	// direction
				{
					if (t > 0.001)
					{
						glm::vec3 intersection = e + t * d;

						glm::vec3 b_a = vertex1_vector - vertex0_vector;
						glm::vec3 x_a = intersection - vertex0_vector;

						glm::vec3 c_b = vertex2_vector - vertex1_vector;
						glm::vec3 x_b = intersection - vertex1_vector;

						glm::vec3 a_c = vertex0_vector - vertex2_vector;
						glm::vec3 x_c = intersection - vertex2_vector;

						bool sign1 = dot(cross(b_a, x_a), n) > 0;
						bool sign2 = dot(cross(c_b, x_b), n) > 0;
						bool sign3 = dot(cross(a_c, x_c), n) > 0;

						if (sign1 && sign2 && sign3) // intersect with this triangle
						{
							return true;
						}
					}
				}
			}
		}//else if
		else if (listOfShapes.at(i)->type == "plane")
		{
			glm::vec3 a = listOfShapes.at(i)->position;
			glm::vec3 n = normalize(listOfShapes.at(i)->normal);
			glm::vec3 d = s - e;

			float denominator = dot(n, d);

			if (denominator != 0)
			{
				float t = dot(n, (a - e)) / denominator;
				if (type == 1 || type == 3)	// point or spot
				{
					if ( t > 0.001 && t < 1 )
					{
						return true;
					}
				}
				else if (type == 2)
				{
					if (t > 0.01)
					{
						return true;
					}
				}
			}
		}//else if
		else if (listOfShapes.at(i)->type == "intersection")
		{
			shape * sub_shape1 = listOfShapes.at(i)->sub_shape1;
			shape * sub_shape2 = listOfShapes.at(i)->sub_shape2;

			// hit testing with first sphere
			bool hit_with_first = false;

			float radius_for_first = sub_shape1->radius;
			glm::vec3 c_for_first = sub_shape1->position;
			glm::vec3 d = s - e;
			float t_For_first = 0;
			float t_For_first2 = 0;
			float determine = glm::pow(dot(d, (e - c_for_first)), 2) - dot(d, d) * (dot((e - c_for_first), (e - c_for_first)) - radius_for_first * radius_for_first);

			if (determine >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine);
				t_For_first = (-1 * dot(d, (e - c_for_first)) - rooted_determine) / dot(d, d);
				t_For_first2 = (-1 * dot(d, (e - c_for_first)) + rooted_determine) / dot(d, d);

				if (t_For_first > 0.001 || t_For_first2 > 0.001)
				{
					hit_with_first = true;
				}
			}
			// hit testing with second sphere
			bool hit_with_second = false;

			float radius_for_second = sub_shape2->radius;
			glm::vec3 c_for_second = sub_shape2->position;
			d = s - e;
			float t_For_second = 0;
			float t_For_second2 = 0;

			float determine_for_second = glm::pow(dot(d, (e - c_for_second)), 2) - dot(d, d) * (dot((e - c_for_second), (e - c_for_second)) - radius_for_second * radius_for_second);
			if (determine_for_second >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine_for_second);
				t_For_second = (-1 * dot(d, (e - c_for_second)) - rooted_determine) / dot(d, d);
				t_For_second2 = (-1 * dot(d, (e - c_for_second)) + rooted_determine) / dot(d, d);

				if (t_For_second > 0.001 || t_For_second2 > 0.001)
				{
					hit_with_second = true;
				}
			}
			if (hit_with_first && hit_with_second)
			{
				return true;
			}
		}
		else if (listOfShapes.at(i)->type == "union")
		{
			shape * sub_shape1 = listOfShapes.at(i)->sub_shape1;
			shape * sub_shape2 = listOfShapes.at(i)->sub_shape2;

			// hit testing with first sphere
			bool hit_with_first = false;

			float radius_for_first = sub_shape1->radius;
			glm::vec3 c_for_first = sub_shape1->position;
			glm::vec3 d = s - e;
			float t_For_first = 0;

			float determine = glm::pow(dot(d, (e - c_for_first)), 2) - dot(d, d) * (dot((e - c_for_first), (e - c_for_first)) - radius_for_first * radius_for_first);

			if (determine >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine);
				t_For_first = (-1 * dot(d, (e - c_for_first)) - rooted_determine) / dot(d, d);

				if (type == 1 || type == 3)	// point or spot
				{
					if (t_For_first > 0.001 && t_For_first < 1)
					{
						hit_with_first = true;
					}
				}
				else if (type == 2)	// direction
				{
					if (t_For_first > 0.001)
					{
						hit_with_first = true;
					}
				}
			}
			// hit testing with second sphere
			bool hit_with_second = false;

			float radius_for_second = sub_shape2->radius;
			glm::vec3 c_for_second = sub_shape2->position;
			d = s - e;
			float t_For_second = 0;

			float determine_for_second = glm::pow(dot(d, (e - c_for_second)), 2) - dot(d, d) * (dot((e - c_for_second), (e - c_for_second)) - radius_for_second * radius_for_second);
			if (determine_for_second >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine_for_second);
				t_For_second = (-1 * dot(d, (e - c_for_second)) - rooted_determine) / dot(d, d);

				if (type == 1 || type == 3)	// point or spot
				{
					if (t_For_second > 0.001 && t_For_second < 1)
					{
						hit_with_second = true;
					}
				}
				else if (type == 2)	// direction
				{
					if (t_For_second > 0.001)
					{
						hit_with_second = true;
					}
				}
			}
			if ( hit_with_first || hit_with_second)
			{
				return true;
			}
		}
		else if (listOfShapes.at(i)->type == "difference")
		{
			shape * sub_shape1 = listOfShapes.at(i)->sub_shape1;
			shape * sub_shape2 = listOfShapes.at(i)->sub_shape2;

			// hit testing with first sphere
			bool hit_with_first = false;

			float radius_for_first = sub_shape1->radius;
			glm::vec3 c_for_first = sub_shape1->position;
			glm::vec3 d = s - e;
			float t_For_first = 0;

			float determine = glm::pow(dot(d, (e - c_for_first)), 2) - dot(d, d) * (dot((e - c_for_first), (e - c_for_first)) - radius_for_first * radius_for_first);

			if (determine >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine);
				t_For_first = (-1 * dot(d, (e - c_for_first)) - rooted_determine) / dot(d, d);

				if (type == 1 || type == 3)	// point or spot
				{
					if (t_For_first > 0.001 && t_For_first < 1)
					{
						hit_with_first = true;
					}
				}
				else if (type == 2)	// direction
				{
					if (t_For_first > 0.001)
					{
						hit_with_first = true;
					}
				}
			}
			// hit testing with second sphere
			bool hit_with_second = false;

			float radius_for_second = sub_shape2->radius;
			glm::vec3 c_for_second = sub_shape2->position;
			d = s - e;
			float t_For_second = 0;

			float determine_for_second = glm::pow(dot(d, (e - c_for_second)), 2) - dot(d, d) * (dot((e - c_for_second), (e - c_for_second)) - radius_for_second * radius_for_second);
			if (determine_for_second >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine_for_second);
				t_For_second = (-1 * dot(d, (e - c_for_second)) - rooted_determine) / dot(d, d);

				if (type == 1 || type == 3)	// point or spot
				{
					if (t_For_second > 0.001 && t_For_second < 1)
					{
						hit_with_second = true;
					}
				}
				else if (type == 2)	// direction
				{
					if (t_For_second > 0.001)
					{
						hit_with_second = true;
					}
				}
			}

			if (hit_with_first && !hit_with_second)
			{
				return true;
			}
		}
	}//for
	return false;
}

bool hitTesting(const point3 &e, const point3 &s, 
				glm::vec3 &material_ambient, glm::vec3 &material_diffuse, glm::vec3 &material_specular,
				float &material_shininess, glm::vec3 &material_reflective, glm::vec3 &material_transmissive, float &material_refraction, float &material_roughness,
				glm::vec3 &intersection, glm::vec3 &N, glm::vec3 &center, int &type, float &radiusParamter, std::vector<shape *> objects_to_for_hit_testing)
{
	bool isHit = false;
	float finalT = 10000.0f;

	for (int i = 0; i < objects_to_for_hit_testing.size(); i++)
	{
		if (objects_to_for_hit_testing.at(i)->type == "sphere")
		{
			float radius = objects_to_for_hit_testing.at(i)->radius;
			glm::vec3 c = objects_to_for_hit_testing.at(i)->position;
			glm::vec3 d = s - e;

			float determine = glm::pow(dot(d, (e - c)), 2) - dot(d, d) * (dot((e - c), (e - c)) - radius * radius);

			if (determine >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine);
				float t = (-1 * dot(d, (e - c)) - rooted_determine) / dot(d, d);

				if (t > 0.001 && t < finalT)
				{
					finalT = t;
					intersection = e + t * d;
					material_ambient = objects_to_for_hit_testing.at(i)->material_ambient;
					material_diffuse = objects_to_for_hit_testing.at(i)->material_diffuse;
					material_specular = objects_to_for_hit_testing.at(i)->material_specular;
					material_roughness = objects_to_for_hit_testing.at(i)->material_roughness;
					material_shininess = objects_to_for_hit_testing.at(i)->material_shininess;
					material_reflective = objects_to_for_hit_testing.at(i)->material_reflective;
					material_transmissive = objects_to_for_hit_testing.at(i)->material_transmissive;
					material_refraction = objects_to_for_hit_testing.at(i)->material_refraction;
					center = c;
					type = 4;
					N = normalize(e + t * d - c);
					radiusParamter = radius;
					isHit = true;
				}
			}
		}// if
		else if (objects_to_for_hit_testing.at(i)->type == "triangle")
		{
			glm::vec3 vertex0_vector = objects_to_for_hit_testing.at(i)->vertex0;
			glm::vec3 vertex1_vector = objects_to_for_hit_testing.at(i)->vertex1;
			glm::vec3 vertex2_vector = objects_to_for_hit_testing.at(i)->vertex2;

			glm::vec3 n = normalize(cross(vertex1_vector - vertex0_vector, vertex2_vector - vertex1_vector));
			glm::vec3 d = s - e;

			float denominator = dot(n, d);

			if (denominator != 0)
			{
				float t = dot(n, (vertex0_vector - e)) / denominator;

				if (t > 0.001 && t < finalT)
				{
					glm::vec3 intersectionForPlane = e + t * d;

					glm::vec3 b_a = vertex1_vector - vertex0_vector;
					glm::vec3 x_a = intersectionForPlane - vertex0_vector;

					glm::vec3 c_b = vertex2_vector - vertex1_vector;
					glm::vec3 x_b = intersectionForPlane - vertex1_vector;

					glm::vec3 a_c = vertex0_vector - vertex2_vector;
					glm::vec3 x_c = intersectionForPlane - vertex2_vector;

					bool sign1 = dot(cross(b_a, x_a), n) > 0;
					bool sign2 = dot(cross(c_b, x_b), n) > 0;
					bool sign3 = dot(cross(a_c, x_c), n) > 0;

					if (sign1 && sign2 && sign3) // intersect with this triangle
					{
						finalT = t;
						intersection = intersectionForPlane;

						material_ambient = objects_to_for_hit_testing.at(i)->material_ambient;
						material_diffuse = objects_to_for_hit_testing.at(i)->material_diffuse;
						material_specular = objects_to_for_hit_testing.at(i)->material_specular;
						material_shininess = objects_to_for_hit_testing.at(i)->material_shininess;
						material_roughness = objects_to_for_hit_testing.at(i)->material_roughness;
						material_reflective = objects_to_for_hit_testing.at(i)->material_reflective;
						material_transmissive = objects_to_for_hit_testing.at(i)->material_transmissive;
						material_refraction = objects_to_for_hit_testing.at(i)->material_refraction;

						N = normalize(n);
						center = glm::vec3(0,0,0);
						type = 5;
						isHit = true;
					}
				}
			}
		}//else if
		else if (objects_to_for_hit_testing.at(i)->type == "plane")
		{
			glm::vec3 a = objects_to_for_hit_testing.at(i)->position;
			glm::vec3 n = normalize(objects_to_for_hit_testing.at(i)->normal);
			glm::vec3 d = s - e;

			float denominator = dot(n, d);

			if (denominator != 0)
			{
				float t = dot(n, (a - e)) / denominator;

				if ( t > 0.001 && t < finalT )
				{
					finalT = t;
					intersection = e + t * d;;

					material_ambient = objects_to_for_hit_testing.at(i)->material_ambient;
					material_diffuse = objects_to_for_hit_testing.at(i)->material_diffuse;
					material_specular = objects_to_for_hit_testing.at(i)->material_specular;
					material_shininess = objects_to_for_hit_testing.at(i)->material_shininess;
					material_roughness = objects_to_for_hit_testing.at(i)->material_roughness;
					material_reflective = objects_to_for_hit_testing.at(i)->material_reflective;
					material_transmissive = objects_to_for_hit_testing.at(i)->material_transmissive;
					material_refraction = objects_to_for_hit_testing.at(i)->material_refraction;

					N = normalize(n);
					center = glm::vec3(0, 0, 0);
					type = 6;
					isHit = true;
				}
			}
		}//else if
		else if (objects_to_for_hit_testing.at(i)->type == "intersection")
		{
			shape * sub_shape1 = objects_to_for_hit_testing.at(i)->sub_shape1;
			shape * sub_shape2 = objects_to_for_hit_testing.at(i)->sub_shape2;

			// hit testing with first sphere
			bool hit_with_first = false;

			float radius_for_first = sub_shape1->radius;
			glm::vec3 c_for_first = sub_shape1->position;
			glm::vec3 d = s - e;
			float t_For_first = 0;

			float determine = glm::pow(dot(d, (e - c_for_first)), 2) - dot(d, d) * (dot((e - c_for_first), (e - c_for_first)) - radius_for_first * radius_for_first);

			if (determine >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine);
				t_For_first = (-1 * dot(d, (e - c_for_first)) - rooted_determine) / dot(d, d);

				if (t_For_first > 0.001 && t_For_first < finalT)
				{
					hit_with_first = true;
				}
			}

			// hit testing with second sphere
			bool hit_with_second = false;

			float radius_for_second = sub_shape2->radius;
			glm::vec3 c_for_second = sub_shape2->position;
			d = s - e;
			float t_For_second = 0;

			float determine_for_second = glm::pow(dot(d, (e - c_for_second)), 2) - dot(d, d) * (dot((e - c_for_second), (e - c_for_second)) - radius_for_second * radius_for_second);
			if (determine_for_second >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine_for_second);
				t_For_second = (-1 * dot(d, (e - c_for_second)) - rooted_determine) / dot(d, d);

				if (t_For_second > 0.001 && t_For_second < finalT)
				{
					hit_with_second = true;
				}
			}

			if ( hit_with_first && hit_with_second )
			{
				finalT = glm::min(t_For_first, t_For_second);
				intersection = e + finalT * d;
				material_ambient = (sub_shape1->material_ambient + sub_shape2->material_ambient) / 2.0f;
				material_diffuse = (sub_shape1->material_diffuse + sub_shape2->material_diffuse) / 2.0f;
				material_specular = (sub_shape1->material_specular + sub_shape2->material_specular) / 2.0f;
				material_roughness = (sub_shape1->material_roughness + sub_shape2->material_roughness) / 2.0f;
				material_shininess = (sub_shape1->material_shininess + sub_shape2->material_shininess) / 2.0f;
				material_reflective = (sub_shape1->material_reflective + sub_shape2->material_reflective) / 2.0f;
				material_transmissive = (sub_shape1->material_transmissive + sub_shape2->material_transmissive) / 2.0f;
				material_refraction = (sub_shape1->material_refraction + sub_shape2->material_refraction) / 2.0f;
				type = 4;
				center = c_for_first;
				N = normalize(e + t_For_first * d - c_for_first);
				radiusParamter = radius_for_first;
			
				isHit = true;
			}
		}
		else if (objects_to_for_hit_testing.at(i)->type == "union")
		{
			shape * sub_shape1 = objects_to_for_hit_testing.at(i)->sub_shape1;
			shape * sub_shape2 = objects_to_for_hit_testing.at(i)->sub_shape2;

			// hit testing with first sphere
			bool hit_with_first = false;

			float radius_for_first = sub_shape1->radius;
			glm::vec3 c_for_first = sub_shape1->position;
			glm::vec3 d = s - e;
			float t_For_first = 0;

			float determine = glm::pow(dot(d, (e - c_for_first)), 2) - dot(d, d) * (dot((e - c_for_first), (e - c_for_first)) - radius_for_first * radius_for_first);

			if (determine >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine);
				t_For_first = (-1 * dot(d, (e - c_for_first)) - rooted_determine) / dot(d, d);

				if (t_For_first > 0.001 && t_For_first < finalT)
				{
					hit_with_first = true;
				}
			}

			// hit testing with second sphere
			bool hit_with_second = false;

			float radius_for_second = sub_shape2->radius;
			glm::vec3 c_for_second = sub_shape2->position;
			d = s - e;
			float t_For_second = 0;

			float determine_for_second = glm::pow(dot(d, (e - c_for_second)), 2) - dot(d, d) * (dot((e - c_for_second), (e - c_for_second)) - radius_for_second * radius_for_second);
			if (determine_for_second >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine_for_second);
				t_For_second = (-1 * dot(d, (e - c_for_second)) - rooted_determine) / dot(d, d);

				if (t_For_second > 0.001 && t_For_second < finalT)
				{
					hit_with_second = true;
				}
			}

			if (hit_with_first && hit_with_second)
			{
				finalT = glm::min(t_For_first, t_For_second);
				intersection = e + finalT * d;
				material_ambient = (sub_shape1->material_ambient + sub_shape2->material_ambient) / 2.0f;
				material_diffuse = (sub_shape1->material_diffuse + sub_shape2->material_diffuse) / 2.0f;
				material_specular = (sub_shape1->material_specular + sub_shape2->material_specular) / 2.0f;
				material_roughness = (sub_shape1->material_roughness + sub_shape2->material_roughness) / 2.0f;
				material_shininess = (sub_shape1->material_shininess + sub_shape2->material_shininess) / 2.0f;
				material_reflective = (sub_shape1->material_reflective + sub_shape2->material_reflective) / 2.0f;
				material_transmissive = (sub_shape1->material_transmissive + sub_shape2->material_transmissive) / 2.0f;
				material_refraction = (sub_shape1->material_refraction + sub_shape2->material_refraction) / 2.0f;
				type = 4;
				center = c_for_first;
				N = normalize(e + t_For_first * d - c_for_first);
				radiusParamter = radius_for_first;

				isHit = true;
			}
			else if (hit_with_first && !hit_with_second)
			{
				finalT = t_For_first;
				intersection = e + t_For_first * d;
				material_ambient = sub_shape1->material_ambient;
				material_diffuse = sub_shape1->material_diffuse;
				material_specular = sub_shape1->material_specular;
				material_roughness = sub_shape1->material_roughness;
				material_shininess = sub_shape1->material_shininess;
				material_reflective = sub_shape1->material_reflective;
				material_transmissive = sub_shape1->material_transmissive;
				material_refraction = sub_shape1->material_refraction;
				type = 4;
				center = c_for_first;
				N = normalize(e + t_For_first * d - c_for_first);
				radiusParamter = radius_for_first;

				isHit = true;
			}
			else if (!hit_with_first && hit_with_second)
			{
				finalT = t_For_second;
				intersection = e + t_For_second * d;
				material_ambient = sub_shape2->material_ambient;
				material_diffuse = sub_shape2->material_diffuse;
				material_specular = sub_shape2->material_specular;
				material_roughness = sub_shape2->material_roughness;
				material_shininess = sub_shape2->material_shininess;
				material_reflective = sub_shape2->material_reflective;
				material_transmissive = sub_shape2->material_transmissive;
				material_refraction = sub_shape2->material_refraction;
				type = 4;
				center = c_for_second;
				N = normalize(e + t_For_second * d - c_for_second);
				radiusParamter = radius_for_second;

				isHit = true;
			}
		}
		else if (objects_to_for_hit_testing.at(i)->type == "difference")
		{
			shape * sub_shape1 = objects_to_for_hit_testing.at(i)->sub_shape1;
			shape * sub_shape2 = objects_to_for_hit_testing.at(i)->sub_shape2;

			// hit testing with first sphere
			bool hit_with_first = false;

			float radius_for_first = sub_shape1->radius;
			glm::vec3 c_for_first = sub_shape1->position;
			glm::vec3 d = s - e;
			float t_For_first = 0;

			float determine = glm::pow(dot(d, (e - c_for_first)), 2) - dot(d, d) * (dot((e - c_for_first), (e - c_for_first)) - radius_for_first * radius_for_first);

			if (determine >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine);
				t_For_first = (-1 * dot(d, (e - c_for_first)) - rooted_determine) / dot(d, d);

				if (t_For_first > 0.001 && t_For_first < finalT)
				{
					hit_with_first = true;
				}
			}

			// hit testing with second sphere
			bool hit_with_second = false;

			float radius_for_second = sub_shape2->radius;
			glm::vec3 c_for_second = sub_shape2->position;
			d = s - e;
			float t_For_second = 0;

			float determine_for_second = glm::pow(dot(d, (e - c_for_second)), 2) - dot(d, d) * (dot((e - c_for_second), (e - c_for_second)) - radius_for_second * radius_for_second);
			if (determine_for_second >= 0) // there is intersetion 
			{
				float rooted_determine = glm::sqrt(determine_for_second);
				t_For_second = (-1 * dot(d, (e - c_for_second)) - rooted_determine) / dot(d, d);

				if (t_For_second > 0.001 && t_For_second < finalT)
				{
					hit_with_second = true;
				}
			}

			if (hit_with_first && !hit_with_second)
			{
				finalT = t_For_first;
				intersection = e + t_For_first * d;
				material_ambient = sub_shape1->material_ambient;
				material_diffuse = sub_shape1->material_diffuse;
				material_specular = sub_shape1->material_specular;
				material_roughness = sub_shape1->material_roughness;
				material_shininess = sub_shape1->material_shininess;
				material_reflective = sub_shape1->material_reflective;
				material_transmissive = sub_shape1->material_transmissive;
				material_refraction = sub_shape1->material_refraction;
				type = 4;
				center = c_for_first;
				N = normalize(e + t_For_first * d - c_for_first);
				radiusParamter = radius_for_first;

				isHit = true;
			}
		}
	}//for
	return isHit;
}

void getColor(colour3 &colour,
			  glm::vec3 &material_ambient, glm::vec3 &material_diffuse, glm::vec3 &material_specular,
			  float &material_shininess, glm::vec3 &material_reflective, glm::vec3 &material_transmissive, float &material_refraction, float &material_roughness,
			  const glm::vec3 &intersection, glm::vec3 &N, glm::vec3 &V)
{
	// Oren每Nayar reflectance diffuse model
	float power_of_material_roughness = material_roughness * material_roughness;
	float A = 1 - 0.5 * (power_of_material_roughness / (power_of_material_roughness + 0.33));
	float B = 0.45 * (power_of_material_roughness / (power_of_material_roughness + 0.09));
	float theta_r = acos(dot(V, N));
	glm::vec3 u = normalize(V - N * glm::clamp(dot(N, V), 0.0f, 1.0f));

	// point
	glm::vec3 colour_diffuse_point = glm::vec3(0, 0, 0);
	glm::vec3 colour_specular_point = glm::vec3(0, 0, 0);

	for (int i = 0; i < light_point_position.size(); i++)
	{
		bool is_shadowed_by_point_light = shadowTesting(intersection, light_point_position.at(i), 1);

		if (!is_shadowed_by_point_light)
		{
			glm::vec3 LforPoint = normalize(light_point_position.at(i) - intersection);
			glm::vec3 HforPoint = normalize(LforPoint + V);

			// Oren每Nayar reflectance diffuse model
			float theta_i = acos(dot(LforPoint, N));
			float alpha = glm::max(theta_i, theta_r);
			float beta = glm::min(theta_i, theta_r);
			glm::vec3 v = normalize(LforPoint - N * glm::clamp(dot(N, LforPoint), 0.0f, 1.0f));

			colour_diffuse_point += material_diffuse * glm::max(dot(N, LforPoint), 0.0f) * (A + (B * glm::max(0.0f, dot(u, v)) * sin(alpha) * tan(beta))) * light_point_color.at(i);

			if (dot(LforPoint, N) < 0.0)
			{
			}
			else
			{
				colour_specular_point += light_point_color.at(i) * material_specular * pow(glm::max(dot(N, HforPoint), 0.0f), material_shininess);
			}
		}
	}

	// directional
	glm::vec3 colour_diffuse_directional = glm::vec3(0, 0, 0);
	glm::vec3 colour_specular_directional = glm::vec3(0, 0, 0);

	for (int i = 0; i < light_directional_direction.size(); i++)
	{
		bool is_shadowed_by_directional_light = shadowTesting(intersection, intersection - light_directional_direction.at(i), 2);

		if (!is_shadowed_by_directional_light)
		{
			glm::vec3 LforDirectional = -normalize(light_directional_direction.at(i));
			glm::vec3 HforDirectional = normalize(LforDirectional + V);

			// Oren每Nayar reflectance diffuse model
			float theta_i = acos(dot(LforDirectional, N));
			float alpha = glm::max(theta_i, theta_r);
			float beta = glm::min(theta_i, theta_r);
			glm::vec3 v = normalize(LforDirectional - N * glm::clamp(dot(N, LforDirectional), 0.0f, 1.0f));

			colour_diffuse_directional += material_diffuse * glm::max(dot(N, LforDirectional), 0.0f) * (A + (B * glm::max(0.0f, dot(u, v)) * sin(alpha) * tan(beta))) * light_directional_color.at(i);
			
			if (dot(LforDirectional, N) < 0.0)
			{
			}
			else
			{
				colour_specular_directional += light_directional_color.at(i) * material_specular * pow(glm::max(dot(N, HforDirectional), 0.0f), material_shininess);
			}
		}
	}

	// spot
	glm::vec3 colour_diffuse_spot = glm::vec3(0, 0, 0);
	glm::vec3 colour_specular_spot = glm::vec3(0, 0, 0);

	for (int i = 0; i < light_spot_direction.size(); i++)
	{
		float angle = acos(dot(light_spot_direction.at(i), intersection - light_spot_position.at(i)) / (length(light_spot_direction.at(i)) * length(intersection - light_spot_position.at(i))));
		if (angle <= glm::radians( light_spot_cutoff.at(i) ) )
		{
			bool is_shadowed_by_spot_light = shadowTesting(intersection, light_spot_position.at(i), 3);

			if (!is_shadowed_by_spot_light)
			{
				glm::vec3 LforSpot = -normalize(light_spot_direction.at(i));
				glm::vec3 HforDirectional = normalize(LforSpot + V);

				// Oren每Nayar reflectance diffuse model
				float theta_i = acos(dot(LforSpot, N));
				float alpha = glm::max(theta_i, theta_r);
				float beta = glm::min(theta_i, theta_r);
				glm::vec3 v = normalize(LforSpot - N * glm::clamp(dot(N, LforSpot), 0.0f, 1.0f));

				colour_diffuse_spot += material_diffuse * glm::max(dot(N, LforSpot), 0.0f) * (A + (B * glm::max(0.0f, dot(u, v)) * sin(alpha) * tan(beta))) * light_spot_color.at(i);
				
				if (dot(LforSpot, N) < 0.0)
				{
				}
				else
				{
					colour_specular_spot += light_spot_color.at(i) * material_specular * pow(glm::max(dot(N, HforDirectional), 0.0f), material_shininess);
				}
			}
		}
	}

	glm::vec3 colour_ambient = light_ambient_color * material_ambient;
	glm::vec3 colour_diffuse = colour_diffuse_directional + colour_diffuse_point + colour_diffuse_spot;
	glm::vec3 colour_specular = colour_specular_directional + colour_specular_point + colour_specular_spot;

	colour = colour + colour_ambient + colour_diffuse + colour_specular;
}

void mirrorReflection(point3 &e, point3 &s, colour3 &colour, int depth,
					  glm::vec3 &material_ambient, glm::vec3 &material_diffuse, glm::vec3 &material_specular,
				      float &material_shininess, glm::vec3 &material_reflective, glm::vec3 &material_transmissive, float &material_refraction, float &material_roughness,
					  glm::vec3 &N, glm::vec3 lastPoint)
{
	glm::vec3 material_ambient_ForHitPoint = glm::vec3(0, 0, 0);
	glm::vec3 material_diffuse_ForHitPoint = glm::vec3(0, 0, 0);
	glm::vec3 material_specular_ForHitPoint = glm::vec3(0, 0, 0);
	float material_shininess_ForHitPoint = 0;
	float material_roughness_ForHitPoint = 0;
	glm::vec3 material_reflective_ForHitPoint = glm::vec3(0, 0, 0);
	glm::vec3 material_transmissive_ForHitPoint = glm::vec3(0, 0, 0);
	float material_refraction_ForHitPoint = refractionOfAir;
	glm::vec3 intersection_ForHitPoint = glm::vec3(0, 0, 0);
	glm::vec3 N_ForHitPoint = glm::vec3(0, 0, 0);
	glm::vec3 c = glm::vec3(0, 0, 0);
	int type = -1;
	float radius = -1;

	std::vector<shape *> objects_to_for_hit_testing_five;
	ray_box_intersection(e, s, ocTree_root, objects_to_for_hit_testing_five, false);
	bool isHit = hitTesting(e, s,
						    material_ambient_ForHitPoint, material_diffuse_ForHitPoint, material_specular_ForHitPoint,
						    material_shininess_ForHitPoint, material_reflective_ForHitPoint, material_transmissive_ForHitPoint, material_refraction_ForHitPoint, material_roughness_ForHitPoint,
						    intersection_ForHitPoint, N_ForHitPoint, c, type, radius, objects_to_for_hit_testing_five);

	glm::vec3 V_ForHitPoint = normalize(e - intersection_ForHitPoint);
	glm::vec3 RforMirror_ForHitPoint = normalize(2 * glm::max(dot(N_ForHitPoint, V_ForHitPoint), 0.0f) * N_ForHitPoint - V_ForHitPoint);

	if ( depth > 0 && isHit && (material_reflective_ForHitPoint.x != 0.0f || material_reflective_ForHitPoint.y != 0.0f || material_reflective_ForHitPoint.z != 0.0f) )
	{
		mirrorReflection(intersection_ForHitPoint, RforMirror_ForHitPoint + intersection_ForHitPoint, colour, depth - 1,
						 material_ambient_ForHitPoint, material_diffuse_ForHitPoint, material_specular_ForHitPoint,
						 material_shininess_ForHitPoint, material_reflective_ForHitPoint, material_transmissive_ForHitPoint, material_refraction_ForHitPoint, material_roughness_ForHitPoint,
						 N_ForHitPoint, e);
	}

	glm::vec3 colour_diffuse_Mirror = glm::vec3(0, 0, 0);
	glm::vec3 colour_specular_Mirror = glm::vec3(0, 0, 0);

	if (isHit)
	{
		glm::vec3 colourForHitPoint = colour;
		getColor(colourForHitPoint,
			material_ambient_ForHitPoint, material_diffuse_ForHitPoint, material_specular_ForHitPoint,
			material_shininess_ForHitPoint, material_reflective_ForHitPoint, material_transmissive_ForHitPoint, material_refraction_ForHitPoint, material_roughness_ForHitPoint,
			intersection_ForHitPoint, N_ForHitPoint, V_ForHitPoint);

		glm::vec3 LforMirror = normalize(intersection_ForHitPoint - e);
		glm::vec3 HforMirror = normalize(LforMirror + normalize(lastPoint - e));

		colour_diffuse_Mirror = material_reflective * colourForHitPoint * material_diffuse * glm::max(dot(N, LforMirror), 0.0f);
		colour_specular_Mirror = material_reflective * colourForHitPoint * material_specular * pow(glm::max(dot(N, HforMirror), 0.0f), material_shininess);

		if (dot(LforMirror, N) < 0.0)
		{
			colour_specular_Mirror = glm::vec3(0.0, 0.0, 0.0);
		}
	}

	glm::vec3 colourForThis = glm::vec3(0, 0, 0);
	getColor(colourForThis,
			material_ambient, material_diffuse, material_specular,
			material_shininess, material_reflective, material_transmissive, material_refraction, material_roughness,
			e, N, normalize(lastPoint - e) );

	colour = colourForThis + colour_diffuse_Mirror + colour_specular_Mirror;
}

void getBoundingAndShapeList ()
{
	bounding.push_back(0); // left
	bounding.push_back(0); // right
	bounding.push_back(0); // down
	bounding.push_back(0); // up
	bounding.push_back(0); // back
	bounding.push_back(-10000); // front

	json &objects = scene["objects"];

	for (json::iterator it = objects.begin(); it != objects.end(); ++it)
	{
		json &object = *it;

		float this_left = 0;
		float this_right = 0;
		float this_down = 0;
		float this_up = 0;
		float this_back = 0;
		float this_front = 0;

		json &material = object["material"];

		bool isTransformation = false;
		float rotation;
		int axisOfrotation;
		float scale_x;
		float scale_y;
		float scale_z;
		float translation_x;
		float translation_y;
		float translation_z;

		if (object.find("transformation") != object.end())
		{
			isTransformation = true;
			json &transformation = object["transformation"];

			rotation = transformation["rotation"];
			axisOfrotation = transformation["axisOfrotation"];

			std::vector<float> scale = transformation["scale"];
			scale_x = scale.at(0);
			scale_y = scale.at(1);
			scale_z = scale.at(2);

			std::vector<float> translation = transformation["translation"];
			translation_x = translation.at(0);
			translation_y = translation.at(1);
			translation_z = translation.at(2);
		}

		shape * newShape = new shape;
		bool isNewShape = true;

		if (object["type"] == "sphere")
		{
			std::vector<float> pos = object["position"];
			float radius = object["radius"];

			this_left = pos[0] - radius;
			this_right = pos[0] + radius;
			this_down = pos[1] - radius;
			this_up = pos[1] + radius;
			this_back = pos[2] - radius;
			this_front = pos[2] + radius;

			newShape->type = "sphere";
			newShape->position.x = pos[0];
			newShape->position.y = pos[1];
			newShape->position.z = pos[2];
			newShape->radius = radius;
			newShape->half_height = -1;

			newShape->bounding.push_back(newShape->position.x - radius);
			newShape->bounding.push_back(newShape->position.x + radius);
			newShape->bounding.push_back(newShape->position.y - radius);
			newShape->bounding.push_back(newShape->position.y + radius);
			newShape->bounding.push_back(newShape->position.z - radius);
			newShape->bounding.push_back(newShape->position.z + radius);
		}// if
		else if (object["type"] == "plane")
		{
			isNewShape = false;
			shape * newPlane = new shape;
			std::vector<float> pos = object["position"];
			std::vector<float> normal = object["normal"];

			newPlane->type = "plane";
			newPlane->position.x = pos[0];
			newPlane->position.y = pos[1];
			newPlane->position.z = pos[2];
			newPlane->normal.x = normal[0];
			newPlane->normal.y = normal[1];
			newPlane->normal.z = normal[2];

			if (material.find("ambient") != material.end())
			{
				std::vector<float> material_ambient_array = material["ambient"];
				newPlane->material_ambient = vector_to_vec3(material_ambient_array);
			}
			else
			{
				newPlane->material_ambient = glm::vec3(0, 0, 0);
			}

			if (material.find("diffuse") != material.end())
			{
				std::vector<float> material_diffuse_array = material["diffuse"];
				newPlane->material_diffuse = vector_to_vec3(material_diffuse_array);
			}
			else
			{
				newPlane->material_diffuse = glm::vec3(0, 0, 0);
			}

			if (material.find("specular") != material.end())
			{
				std::vector<float> material_specular_array = material["specular"];
				newPlane->material_specular = vector_to_vec3(material_specular_array);
				newPlane->material_shininess = material["shininess"];
			}
			else
			{
				newPlane->material_specular = glm::vec3(0, 0, 0);
				newPlane->material_shininess = 0;
			}

			if (material.find("reflective") != material.end())
			{
				std::vector<float> material_reflective_array = material["reflective"];
				newPlane->material_reflective = vector_to_vec3(material_reflective_array);
			}
			else
			{
				newPlane->material_reflective = glm::vec3(0, 0, 0);
			}

			if (material.find("transmissive") != material.end())
			{
				std::vector<float> material_transmissive_array = material["transmissive"];
				newPlane->material_transmissive = vector_to_vec3(material_transmissive_array);
			}
			else
			{
				newPlane->material_transmissive = glm::vec3(0, 0, 0);
			}

			if (material.find("refraction") != material.end())
			{
				newPlane->material_refraction = material["refraction"];
			}
			else
			{
				newPlane->material_refraction = refractionOfAir;
			}

			if (material.find("roughness") != material.end())
			{
				newPlane->material_roughness = material["roughness"];
			}
			else
			{
				newPlane->material_roughness = 0;
			}

			listOfPlanes.push_back(newPlane);
		}//else if
		else if (object["type"] == "mesh")
		{
			isNewShape = false;
			json &triangles = object["triangles"];
			glm::vec4 bary_center;
			int number = 0;
			float sum_of_x = 0;
			float sum_of_y = 0;
			float sum_of_z = 0;

			for (json::iterator it2 = triangles.begin(); it2 != triangles.end(); ++it2)
			{
				json &triangle = *it2;

				std::vector<float> vertex0 = triangle[0];
				std::vector<float> vertex1 = triangle[1];
				std::vector<float> vertex2 = triangle[2];

				sum_of_x += vertex0.at(0) + vertex1.at(0) + vertex2.at(0);
				sum_of_y += vertex0.at(1) + vertex1.at(1) + vertex2.at(1);
				sum_of_z += vertex0.at(2) + vertex1.at(2) + vertex2.at(2);
				number += 3;
			}

			bary_center.x = sum_of_x / number;
			bary_center.y = sum_of_y / number; 
			bary_center.z = sum_of_z / number;
			bary_center.w = 0.0f;
			std::cout << bary_center.x << " " << bary_center.y << " " << bary_center.z << std::endl;

			for (json::iterator it2 = triangles.begin(); it2 != triangles.end(); ++it2)
			{
				json &triangle = *it2;
				shape * newShape2 = new shape;
				newShape2->type = "triangle";

				std::vector<float> vertex0 = triangle[0];
				newShape2->vertex0 = glm::vec4(vector_to_vec3(vertex0), 0.0f);

				std::vector<float> vertex1 = triangle[1];
				newShape2->vertex1 = glm::vec4(vector_to_vec3(vertex1), 0.0f);

				std::vector<float> vertex2 = triangle[2];
				newShape2->vertex2 = glm::vec4(vector_to_vec3(vertex2), 0.0f);

				// transformation
				if (isTransformation)
				{
					glm::vec3 axis(0.0f, 0.0f, 0.0f);

					if (axisOfrotation == 1)
					{
						axis.x = 1.0f;
					}
					else if (axisOfrotation == 2)
					{
						axis.y = 1.0f;
					}
					else
					{
						axis.z = 1.0f;
					}

					newShape2->vertex0 = newShape2->vertex0 - bary_center;
					newShape2->vertex1 = newShape2->vertex1 - bary_center;
					newShape2->vertex2 = newShape2->vertex2 - bary_center;

					newShape2->vertex0 = glm::scale(glm::mat4(), glm::vec3(scale_x, scale_y, scale_z)) * glm::rotate(glm::mat4(), glm::radians(rotation), axis) * newShape2->vertex0;
					newShape2->vertex0 = newShape2->vertex0 + bary_center + glm::vec4(translation_x, translation_y, translation_z, 0.0);;

					newShape2->vertex1 = glm::scale(glm::mat4(), glm::vec3(scale_x, scale_y, scale_z)) * glm::rotate(glm::mat4(), glm::radians(rotation), axis) * newShape2->vertex1;
					newShape2->vertex1 = newShape2->vertex1 + bary_center + glm::vec4(translation_x, translation_y, translation_z, 0.0);;

					newShape2->vertex2 = glm::scale(glm::mat4(), glm::vec3(scale_x, scale_y, scale_z)) * glm::rotate(glm::mat4(), glm::radians(rotation), axis) * newShape2->vertex2;
					newShape2->vertex2 = newShape2->vertex2 + bary_center + glm::vec4(translation_x, translation_y, translation_z, 0.0);
				}

				float triangle_left = glm::min(glm::min(newShape2->vertex0.x, newShape2->vertex1.x), newShape2->vertex2.x);
				float triangle_right = glm::max(glm::max(newShape2->vertex0.x, newShape2->vertex1.x), newShape2->vertex2.x);

				float triangle_down = glm::min(glm::min(newShape2->vertex0.y, newShape2->vertex1.y), newShape2->vertex2.y);
				float triangle_up = glm::max(glm::max(newShape2->vertex0.y, newShape2->vertex1.y), newShape2->vertex2.y);

				float triangle_back = glm::min(glm::min(newShape2->vertex0.z, newShape2->vertex1.z), newShape2->vertex2.z);
				float triangle_front = glm::max(glm::max(newShape2->vertex0.z, newShape2->vertex1.z), newShape2->vertex2.z);

				if (triangle_left < this_left)
				{
					this_left = triangle_left;
				}
				if (triangle_right > this_right)
				{
					this_right = triangle_right;
				}
				if (triangle_down < this_down)
				{
					this_down = triangle_down;
				}
				if (triangle_up > this_up)
				{
					this_up = triangle_up;
				}
				if (triangle_back < this_back)
				{
					this_back = triangle_back;
				}
				if (triangle_front > this_front)
				{
					this_front = triangle_front;
				}

				newShape2->bounding.push_back(triangle_left);
				newShape2->bounding.push_back(triangle_right);
				newShape2->bounding.push_back(triangle_down);
				newShape2->bounding.push_back(triangle_up);
				newShape2->bounding.push_back(triangle_back);
				newShape2->bounding.push_back(triangle_front);

				if (material.find("ambient") != material.end())
				{
					std::vector<float> material_ambient_array = material["ambient"];
					newShape2->material_ambient = vector_to_vec3(material_ambient_array);
				}
				else
				{
					newShape2->material_ambient = glm::vec3(0, 0, 0);
				}

				if (material.find("diffuse") != material.end())
				{
					std::vector<float> material_diffuse_array = material["diffuse"];
					newShape2->material_diffuse = vector_to_vec3(material_diffuse_array);
				}
				else
				{
					newShape2->material_diffuse = glm::vec3(0, 0, 0);
				}

				if (material.find("specular") != material.end())
				{
					std::vector<float> material_specular_array = material["specular"];
					newShape2->material_specular = vector_to_vec3(material_specular_array);
					newShape2->material_shininess = material["shininess"];
				}
				else
				{
					newShape2->material_specular = glm::vec3(0, 0, 0);
					newShape2->material_shininess = 0;
				}

				if (material.find("reflective") != material.end())
				{
					std::vector<float> material_reflective_array = material["reflective"];
					newShape2->material_reflective = vector_to_vec3(material_reflective_array);
				}
				else
				{
					newShape2->material_reflective = glm::vec3(0, 0, 0);
				}

				if (material.find("transmissive") != material.end())
				{
					std::vector<float> material_transmissive_array = material["transmissive"];
					newShape2->material_transmissive = vector_to_vec3(material_transmissive_array);
				}
				else
				{
					newShape2->material_transmissive = glm::vec3(0, 0, 0);
				}

				if (material.find("refraction") != material.end())
				{
					newShape2->material_refraction = material["refraction"];
				}
				else
				{
					newShape2->material_refraction = refractionOfAir;
				}

				if (material.find("roughness") != material.end())
				{
					newShape2->material_roughness = material["roughness"];
				}
				else
				{
					newShape2->material_roughness = 0;
				}

				listOfShapes.push_back(newShape2);
			}
		}
		else if (object["type"] == "intersection" || object["type"] == "union" || object["type"] == "difference")
		{
			isNewShape = false;

			json & sub_objects = object["objects"];

			json & sub_object1 = sub_objects[0];
			json & sub_object2 = sub_objects[1];

			shape * sub_shape1 = new shape;
			shape * sub_shape2 = new shape;

			// sub_shape1
			std::vector<float> pos = sub_object1["position"];
			sub_shape1->radius = sub_object1["radius"];
			sub_shape1->position.x = pos.at(0);
			sub_shape1->position.y = pos.at(1);
			sub_shape1->position.z = pos.at(2);

			json &material_for_sub_shape1 = sub_object1["material"];

			if (material_for_sub_shape1.find("ambient") != material_for_sub_shape1.end())
			{
				std::vector<float> material_ambient_array = material_for_sub_shape1["ambient"];
				sub_shape1->material_ambient = vector_to_vec3(material_ambient_array);
			}
			else
			{
				sub_shape1->material_ambient = glm::vec3(0, 0, 0);
			}

			if (material_for_sub_shape1.find("diffuse") != material_for_sub_shape1.end())
			{
				std::vector<float> material_diffuse_array = material_for_sub_shape1["diffuse"];
				sub_shape1->material_diffuse = vector_to_vec3(material_diffuse_array);
			}
			else
			{
				sub_shape1->material_diffuse = glm::vec3(0, 0, 0);
			}

			if (material_for_sub_shape1.find("specular") != material_for_sub_shape1.end())
			{
				std::vector<float> material_specular_array = material_for_sub_shape1["specular"];
				sub_shape1->material_specular = vector_to_vec3(material_specular_array);
				sub_shape1->material_shininess = material_for_sub_shape1["shininess"];
			}
			else
			{
				sub_shape1->material_specular = glm::vec3(0, 0, 0);
				sub_shape1->material_shininess = 0;
			}

			if (material_for_sub_shape1.find("reflective") != material_for_sub_shape1.end())
			{
				std::vector<float> material_reflective_array = material_for_sub_shape1["reflective"];
				sub_shape1->material_reflective = vector_to_vec3(material_reflective_array);
			}
			else
			{
				sub_shape1->material_reflective = glm::vec3(0, 0, 0);
			}

			if (material_for_sub_shape1.find("transmissive") != material_for_sub_shape1.end())
			{
				std::vector<float> material_transmissive_array = material_for_sub_shape1["transmissive"];
				sub_shape1->material_transmissive = vector_to_vec3(material_transmissive_array);
			}
			else
			{
				sub_shape1->material_transmissive = glm::vec3(0, 0, 0);
			}

			if (material_for_sub_shape1.find("refraction") != material_for_sub_shape1.end())
			{
				sub_shape1->material_refraction = material_for_sub_shape1["refraction"];
			}
			else
			{
				sub_shape1->material_refraction = refractionOfAir;
			}

			if (material_for_sub_shape1.find("roughness") != material_for_sub_shape1.end())
			{
				sub_shape1->material_roughness = material_for_sub_shape1["roughness"];
			}
			else
			{
				sub_shape1->material_roughness = 0;
			}

			// sub_shape2
			std::vector<float> pos_sub_shape2 = sub_object2["position"];
			sub_shape2->radius = sub_object2["radius"];
			sub_shape2->position.x = pos_sub_shape2.at(0);
			sub_shape2->position.y = pos_sub_shape2.at(1);
			sub_shape2->position.z = pos_sub_shape2.at(2);

			json &material_for_sub_shape2 = sub_object2["material"];

			if (material_for_sub_shape2.find("ambient") != material_for_sub_shape2.end())
			{
				std::vector<float> material_ambient_array = material_for_sub_shape2["ambient"];
				sub_shape2->material_ambient = vector_to_vec3(material_ambient_array);
			}
			else
			{
				sub_shape2->material_ambient = glm::vec3(0, 0, 0);
			}

			if (material_for_sub_shape2.find("diffuse") != material_for_sub_shape2.end())
			{
				std::vector<float> material_diffuse_array = material_for_sub_shape2["diffuse"];
				sub_shape2->material_diffuse = vector_to_vec3(material_diffuse_array);
			}
			else
			{
				sub_shape2->material_diffuse = glm::vec3(0, 0, 0);
			}

			if (material_for_sub_shape2.find("specular") != material_for_sub_shape2.end())
			{
				std::vector<float> material_specular_array = material_for_sub_shape2["specular"];
				sub_shape2->material_specular = vector_to_vec3(material_specular_array);
				sub_shape2->material_shininess = material_for_sub_shape2["shininess"];
			}
			else
			{
				sub_shape2->material_specular = glm::vec3(0, 0, 0);
				sub_shape2->material_shininess = 0;
			}

			if (material_for_sub_shape2.find("reflective") != material_for_sub_shape2.end())
			{
				std::vector<float> material_reflective_array = material_for_sub_shape2["reflective"];
				sub_shape2->material_reflective = vector_to_vec3(material_reflective_array);
			}
			else
			{
				sub_shape2->material_reflective = glm::vec3(0, 0, 0);
			}

			if (material_for_sub_shape2.find("transmissive") != material_for_sub_shape2.end())
			{
				std::vector<float> material_transmissive_array = material_for_sub_shape2["transmissive"];
				sub_shape2->material_transmissive = vector_to_vec3(material_transmissive_array);
			}
			else
			{
				sub_shape2->material_transmissive = glm::vec3(0, 0, 0);
			}

			if (material_for_sub_shape2.find("refraction") != material_for_sub_shape2.end())
			{
				sub_shape2->material_refraction = material_for_sub_shape2["refraction"];
			}
			else
			{
				sub_shape2->material_refraction = refractionOfAir;
			}

			if (material_for_sub_shape2.find("roughness") != material_for_sub_shape2.end())
			{
				sub_shape2->material_roughness = material_for_sub_shape2["roughness"];
			}
			else
			{
				sub_shape2->material_roughness = 0;
			}
			shape * new_shape2 = new shape;

			if (object["type"] == "intersection")
			{
				new_shape2->type = "intersection";
			}
			else if(object["type"] == "union")
			{
				new_shape2->type = "union";
			}
			else if (object["type"] == "difference")
			{
				new_shape2->type = "difference";
			}
			new_shape2->sub_shape1 = sub_shape1;
			new_shape2->sub_shape2 = sub_shape2;

			this_left = glm::min(sub_shape1->position.x - sub_shape1->radius, sub_shape2->position.x - sub_shape2->radius);
			this_right = glm::max(sub_shape1->position.x + sub_shape1->radius, sub_shape2->position.x + sub_shape2->radius);
			this_down = glm::min(sub_shape1->position.y - sub_shape1->radius, sub_shape2->position.y - sub_shape2->radius);
			this_up = glm::max(sub_shape1->position.y + sub_shape1->radius, sub_shape2->position.y + sub_shape2->radius);
			this_back = glm::min(sub_shape1->position.z - sub_shape1->radius, sub_shape2->position.z - sub_shape2->radius);
			this_front = glm::max(sub_shape1->position.z + sub_shape1->radius, sub_shape2->position.z + sub_shape2->radius);

			new_shape2->bounding.push_back(this_left);
			new_shape2->bounding.push_back(this_right);
			new_shape2->bounding.push_back(this_down);
			new_shape2->bounding.push_back(this_up);
			new_shape2->bounding.push_back(this_back);
			new_shape2->bounding.push_back(this_front);

			listOfShapes.push_back(new_shape2);
		}//else if

		if (isNewShape)
		{
			if (material.find("ambient") != material.end())
			{
				std::vector<float> material_ambient_array = material["ambient"];
				newShape->material_ambient = vector_to_vec3(material_ambient_array);
			}
			else
			{
				newShape->material_ambient = glm::vec3(0, 0, 0);
			}

			if (material.find("diffuse") != material.end())
			{
				std::vector<float> material_diffuse_array = material["diffuse"];
				newShape->material_diffuse = vector_to_vec3(material_diffuse_array);
			}
			else
			{
				newShape->material_diffuse = glm::vec3(0, 0, 0);
			}

			if (material.find("specular") != material.end())
			{
				std::vector<float> material_specular_array = material["specular"];
				newShape->material_specular = vector_to_vec3(material_specular_array);
				newShape->material_shininess = material["shininess"];
			}
			else
			{
				newShape->material_specular = glm::vec3(0, 0, 0);
				newShape->material_shininess = 0;
			}

			if (material.find("reflective") != material.end())
			{
				std::vector<float> material_reflective_array = material["reflective"];
				newShape->material_reflective = vector_to_vec3(material_reflective_array);
			}
			else
			{
				newShape->material_reflective = glm::vec3(0, 0, 0);
			}

			if (material.find("transmissive") != material.end())
			{
				std::vector<float> material_transmissive_array = material["transmissive"];
				newShape->material_transmissive = vector_to_vec3(material_transmissive_array);
			}
			else
			{
				newShape->material_transmissive = glm::vec3(0, 0, 0);
			}

			if (material.find("refraction") != material.end())
			{
				newShape->material_refraction = material["refraction"];
			}
			else
			{
				newShape->material_refraction = refractionOfAir;
			}

			if (material.find("roughness") != material.end())
			{
				newShape->material_roughness = material["roughness"];
			}
			else
			{
				newShape->material_roughness = 0;
			}

			listOfShapes.push_back(newShape);
		}

		if (object["type"] == "sphere" || object["type"] == "mesh" || object["type"] == "intersection" || object["type"] == "union" || object["type"] == "difference")
		{
			if (this_left < bounding.at(0))
			{
				bounding.at(0) = this_left;
			}
			if (this_right > bounding.at(1))
			{
				bounding.at(1) = this_right;
			}

			if (this_down < bounding.at(2))
			{
				bounding.at(2) = this_down;
			}
			if (this_up > bounding.at(3))
			{
				bounding.at(3) = this_up;
			}

			if (this_back < bounding.at(4))
			{
				bounding.at(4) = this_back;
			}
			if (this_front > bounding.at(5))
			{
				bounding.at(5) = this_front;
			}
		}
	}//for

	std::cout << "Bounding: " << bounding.at(0) << " " << bounding.at(1) << " " << bounding.at(2) << " " << bounding.at(3) << " " << bounding.at(4) << " " << bounding.at(5) << std::endl;

	ocTree_root = new Node;
	ocTree_root->bounding = bounding;
	ocTree_root->shapes_contained = listOfShapes;

	partition(ocTree_root, listOfShapes, bounding);
}

void partition( Node * parent, std::vector<shape *> listOfShapes, std::vector<float> bounding )
{
	if ( listOfShapes.size() > 1 
		&& bounding.at(1) - bounding.at(0) > 0.2 
		&& bounding.at(3) - bounding.at(2) > 0.2
		&& bounding.at(5) - bounding.at(4) > 0.2
	   )
	{
		std::vector<float> sub_bounding1;
			sub_bounding1.push_back(bounding.at(0));
			sub_bounding1.push_back((bounding.at(1) + bounding.at(0)) / 2.0f);
			sub_bounding1.push_back(bounding.at(2));
			sub_bounding1.push_back((bounding.at(3) + bounding.at(2)) / 2.0f);
			sub_bounding1.push_back((bounding.at(4) + bounding.at(5)) / 2.0f);
			sub_bounding1.push_back(bounding.at(5));

		std::vector<float> sub_bounding2;
			sub_bounding2.push_back((bounding.at(1) + bounding.at(0)) / 2.0f);
			sub_bounding2.push_back( bounding.at(1) );
			sub_bounding2.push_back(bounding.at(2));
			sub_bounding2.push_back((bounding.at(3) + bounding.at(2)) / 2.0f);
			sub_bounding2.push_back((bounding.at(4) + bounding.at(5)) / 2.0f);
			sub_bounding2.push_back(bounding.at(5));

		std::vector<float> sub_bounding3;
			sub_bounding3.push_back(bounding.at(0));
			sub_bounding3.push_back((bounding.at(1) + bounding.at(0)) / 2.0f);
			sub_bounding3.push_back((bounding.at(3) + bounding.at(2)) / 2.0f);
			sub_bounding3.push_back(bounding.at(3));
			sub_bounding3.push_back((bounding.at(4) + bounding.at(5)) / 2.0f);
			sub_bounding3.push_back(bounding.at(5));

		std::vector<float> sub_bounding4;
			sub_bounding4.push_back((bounding.at(1) + bounding.at(0)) / 2.0f);
			sub_bounding4.push_back(bounding.at(1));
			sub_bounding4.push_back((bounding.at(3) + bounding.at(2)) / 2.0f);
			sub_bounding4.push_back(bounding.at(3));
			sub_bounding4.push_back((bounding.at(4) + bounding.at(5)) / 2.0f);
			sub_bounding4.push_back(bounding.at(5));

		std::vector<float> sub_bounding5;
			sub_bounding5.push_back(bounding.at(0));
			sub_bounding5.push_back((bounding.at(1) + bounding.at(0)) / 2.0f);
			sub_bounding5.push_back(bounding.at(2));
			sub_bounding5.push_back((bounding.at(3) + bounding.at(2)) / 2.0f);
			sub_bounding5.push_back( bounding.at(4) );
			sub_bounding5.push_back( (bounding.at(4) + bounding.at(5) ) / 2.0f);

		std::vector<float> sub_bounding6;
			sub_bounding6.push_back((bounding.at(1) + bounding.at(0)) / 2.0f);
			sub_bounding6.push_back(bounding.at(1));
			sub_bounding6.push_back(bounding.at(2));
			sub_bounding6.push_back((bounding.at(3) + bounding.at(2)) / 2.0f);
			sub_bounding6.push_back(bounding.at(4));
			sub_bounding6.push_back((bounding.at(4) + bounding.at(5)) / 2.0f);

		std::vector<float> sub_bounding7;
			sub_bounding7.push_back(bounding.at(0));
			sub_bounding7.push_back((bounding.at(1) + bounding.at(0)) / 2.0f);
			sub_bounding7.push_back((bounding.at(3) + bounding.at(2)) / 2.0f);
			sub_bounding7.push_back(bounding.at(3));
			sub_bounding7.push_back(bounding.at(4));
			sub_bounding7.push_back((bounding.at(4) + bounding.at(5)) / 2.0f);

		std::vector<float> sub_bounding8;
			sub_bounding8.push_back((bounding.at(1) + bounding.at(0)) / 2.0f);
			sub_bounding8.push_back(bounding.at(1));
			sub_bounding8.push_back((bounding.at(3) + bounding.at(2)) / 2.0f);
			sub_bounding8.push_back(bounding.at(3));
			sub_bounding8.push_back(bounding.at(4));
			sub_bounding8.push_back((bounding.at(4) + bounding.at(5)) / 2.0f);

		//sub_bounding1
		Node * node_sub_bounding1 = new Node;
		node_sub_bounding1->bounding = sub_bounding1;
		// travese all shapes to find those overlap with this sub_bounding.
		for ( int i = 0; i < listOfShapes.size(); i++ )
		{			
			//if ( listOfShapes.at(i)->type != "plane" )
			//{
				if ((sub_bounding1.at(0) <= listOfShapes.at(i)->bounding.at(1) && sub_bounding1.at(1) >= listOfShapes.at(i)->bounding.at(0))
					&&
					(sub_bounding1.at(3) >= listOfShapes.at(i)->bounding.at(2) && sub_bounding1.at(2) <= listOfShapes.at(i)->bounding.at(3))
					&&
					(sub_bounding1.at(4) <= listOfShapes.at(i)->bounding.at(5) && sub_bounding1.at(5) >= listOfShapes.at(i)->bounding.at(4))
					)
				{
					node_sub_bounding1->shapes_contained.push_back(listOfShapes.at(i));
				}
			//}
		}
		// if there is any shapes that overlaps with this sub_bounding, 
		if( node_sub_bounding1->shapes_contained.size() != 0 )
		{
			parent->children.push_back(node_sub_bounding1);
			partition(node_sub_bounding1, node_sub_bounding1->shapes_contained, node_sub_bounding1->bounding);
		}

		//sub_bounding2
		Node * node_sub_bounding2 = new Node;
		node_sub_bounding2->bounding = sub_bounding2;
		// travese all shapes to find those overlap with this sub_bounding.
		for (int i = 0; i < listOfShapes.size(); i++)
		{
			//if (listOfShapes.at(i)->type != "plane")
			//{
				if ((sub_bounding2.at(0) <= listOfShapes.at(i)->bounding.at(1) && sub_bounding2.at(1) >= listOfShapes.at(i)->bounding.at(0))
					&&
					(sub_bounding2.at(3) >= listOfShapes.at(i)->bounding.at(2) && sub_bounding2.at(2) <= listOfShapes.at(i)->bounding.at(3))
					&&
					(sub_bounding2.at(4) <= listOfShapes.at(i)->bounding.at(5) && sub_bounding2.at(5) >= listOfShapes.at(i)->bounding.at(4))
					)
				{
					node_sub_bounding2->shapes_contained.push_back(listOfShapes.at(i));
				}
			//}
		}
		// if there is any shapes that overlaps with this sub_bounding, 
		if (node_sub_bounding2->shapes_contained.size() != 0)
		{
			parent->children.push_back(node_sub_bounding2);
			partition(node_sub_bounding2, node_sub_bounding2->shapes_contained, node_sub_bounding2->bounding);
		}

		//sub_bounding3
		Node * node_sub_bounding3 = new Node;
		node_sub_bounding3->bounding = sub_bounding3;
		// travese all shapes to find those overlap with this sub_bounding.
		for (int i = 0; i < listOfShapes.size(); i++)
		{
			//if (listOfShapes.at(i)->type != "plane")
			//{
				if ((sub_bounding3.at(0) <= listOfShapes.at(i)->bounding.at(1) && sub_bounding3.at(1) >= listOfShapes.at(i)->bounding.at(0))
					&&
					(sub_bounding3.at(3) >= listOfShapes.at(i)->bounding.at(2) && sub_bounding3.at(2) <= listOfShapes.at(i)->bounding.at(3))
					&&
					(sub_bounding3.at(4) <= listOfShapes.at(i)->bounding.at(5) && sub_bounding3.at(5) >= listOfShapes.at(i)->bounding.at(4))
					)
				{
					node_sub_bounding3->shapes_contained.push_back(listOfShapes.at(i));
				}
			//}
		}
		// if there is any shapes that overlaps with this sub_bounding, 
		if (node_sub_bounding3->shapes_contained.size() != 0)
		{
			parent->children.push_back(node_sub_bounding3);
			partition(node_sub_bounding3, node_sub_bounding3->shapes_contained, node_sub_bounding3->bounding);
		}

		//sub_bounding4
		Node * node_sub_bounding4 = new Node;
		node_sub_bounding4->bounding = sub_bounding4;
		// travese all shapes to find those overlap with this sub_bounding.
		for (int i = 0; i < listOfShapes.size(); i++)
		{
			//if (listOfShapes.at(i)->type != "plane")
			//{
				if ((sub_bounding4.at(0) <= listOfShapes.at(i)->bounding.at(1) && sub_bounding4.at(1) >= listOfShapes.at(i)->bounding.at(0))
					&&
					(sub_bounding4.at(3) >= listOfShapes.at(i)->bounding.at(2) && sub_bounding4.at(2) <= listOfShapes.at(i)->bounding.at(3))
					&&
					(sub_bounding4.at(4) <= listOfShapes.at(i)->bounding.at(5) && sub_bounding4.at(5) >= listOfShapes.at(i)->bounding.at(4))
					)
				{
					node_sub_bounding4->shapes_contained.push_back(listOfShapes.at(i));
				}
			//}
		}
		// if there is any shapes that overlaps with this sub_bounding, 
		if (node_sub_bounding4->shapes_contained.size() != 0)
		{
			parent->children.push_back(node_sub_bounding4);
			partition(node_sub_bounding4, node_sub_bounding4->shapes_contained, node_sub_bounding4->bounding);
		}

		//sub_bounding5
		Node * node_sub_bounding5 = new Node;
		node_sub_bounding5->bounding = sub_bounding5;
		// travese all shapes to find those overlap with this sub_bounding.
		for (int i = 0; i < listOfShapes.size(); i++)
		{
			//if (listOfShapes.at(i)->type != "plane")
			//{
				if ((sub_bounding5.at(0) <= listOfShapes.at(i)->bounding.at(1) && sub_bounding5.at(1) >= listOfShapes.at(i)->bounding.at(0))
					&&
					(sub_bounding5.at(3) >= listOfShapes.at(i)->bounding.at(2) && sub_bounding5.at(2) <= listOfShapes.at(i)->bounding.at(3))
					&&
					(sub_bounding5.at(4) <= listOfShapes.at(i)->bounding.at(5) && sub_bounding5.at(5) >= listOfShapes.at(i)->bounding.at(4))
					)
				{
					node_sub_bounding5->shapes_contained.push_back(listOfShapes.at(i));
				}
			//}
		}
		// if there is any shapes that overlaps with this sub_bounding, 
		if (node_sub_bounding5->shapes_contained.size() != 0)
		{
			parent->children.push_back(node_sub_bounding5);
			partition(node_sub_bounding5, node_sub_bounding5->shapes_contained, node_sub_bounding5->bounding);
		}

		//sub_bounding6
		Node * node_sub_bounding6 = new Node;
		node_sub_bounding6->bounding = sub_bounding6;
		// travese all shapes to find those overlap with this sub_bounding.
		for (int i = 0; i < listOfShapes.size(); i++)
		{
			//if (listOfShapes.at(i)->type != "plane")
			//{
				if ((sub_bounding6.at(0) <= listOfShapes.at(i)->bounding.at(1) && sub_bounding6.at(1) >= listOfShapes.at(i)->bounding.at(0))
					&&
					(sub_bounding6.at(3) >= listOfShapes.at(i)->bounding.at(2) && sub_bounding6.at(2) <= listOfShapes.at(i)->bounding.at(3))
					&&
					(sub_bounding6.at(4) <= listOfShapes.at(i)->bounding.at(5) && sub_bounding6.at(5) >= listOfShapes.at(i)->bounding.at(4))
					)
				{
					node_sub_bounding6->shapes_contained.push_back(listOfShapes.at(i));
				}
			//}
		}
		// if there is any shapes that overlaps with this sub_bounding, 
		if (node_sub_bounding6->shapes_contained.size() != 0)
		{
			parent->children.push_back(node_sub_bounding6);
			partition(node_sub_bounding6, node_sub_bounding6->shapes_contained, node_sub_bounding6->bounding);
		}

		//sub_bounding7
		Node * node_sub_bounding7 = new Node;
		node_sub_bounding7->bounding = sub_bounding7;
		// travese all shapes to find those overlap with this sub_bounding.
		for (int i = 0; i < listOfShapes.size(); i++)
		{
			//if (listOfShapes.at(i)->type != "plane")
			//{
				if ((sub_bounding7.at(0) <= listOfShapes.at(i)->bounding.at(1) && sub_bounding7.at(1) >= listOfShapes.at(i)->bounding.at(0))
					&&
					(sub_bounding7.at(3) >= listOfShapes.at(i)->bounding.at(2) && sub_bounding7.at(2) <= listOfShapes.at(i)->bounding.at(3))
					&&
					(sub_bounding7.at(4) <= listOfShapes.at(i)->bounding.at(5) && sub_bounding7.at(5) >= listOfShapes.at(i)->bounding.at(4))
					)
				{
					node_sub_bounding7->shapes_contained.push_back(listOfShapes.at(i));
				}
			//}
		}
		// if there is any shapes that overlaps with this sub_bounding, 
		if (node_sub_bounding7->shapes_contained.size() != 0)
		{
			parent->children.push_back(node_sub_bounding7);
			partition(node_sub_bounding7, node_sub_bounding7->shapes_contained, node_sub_bounding7->bounding);
		}

		//sub_bounding8
		Node * node_sub_bounding8 = new Node;
		node_sub_bounding8->bounding = sub_bounding8;
		// travese all shapes to find those overlap with this sub_bounding.
		for (int i = 0; i < listOfShapes.size(); i++)
		{
			//if (listOfShapes.at(i)->type != "plane")
			//{
				if ((sub_bounding8.at(0) <= listOfShapes.at(i)->bounding.at(1) && sub_bounding8.at(1) >= listOfShapes.at(i)->bounding.at(0))
					&&
					(sub_bounding8.at(3) >= listOfShapes.at(i)->bounding.at(2) && sub_bounding8.at(2) <= listOfShapes.at(i)->bounding.at(3))
					&&
					(sub_bounding8.at(4) <= listOfShapes.at(i)->bounding.at(5) && sub_bounding8.at(5) >= listOfShapes.at(i)->bounding.at(4))
					)
				{
					node_sub_bounding8->shapes_contained.push_back(listOfShapes.at(i));
				}
			//}
		}
		// if there is any shapes that overlaps with this sub_bounding, 
		if (node_sub_bounding8->shapes_contained.size() != 0)
		{
			parent->children.push_back(node_sub_bounding8);
			partition(node_sub_bounding8, node_sub_bounding8->shapes_contained, node_sub_bounding8->bounding);
		}
	}
}

void ray_box_intersection(glm::vec3 &e, glm::vec3 &s, Node * node, std::vector<shape *> &objects_to_for_hit_testing, bool pick)
{
	float txmin, txmax;
	float tymin, tymax;
	float tzmin, tzmax;

	bool isIntersected = true;

	if ((s - e).x >= 0)
	{
		txmin = (node->bounding.at(0) - e.x) / (s - e).x;
		txmax = (node->bounding.at(1) - e.x) / (s - e).x;
	}
	else
	{
		txmin = (node->bounding.at(1) - e.x) / (s - e).x;
		txmax = (node->bounding.at(0) - e.x) / (s - e).x;
	}

	if ((s - e).y >= 0)
	{
		tymin = (node->bounding.at(2) - e.y) / (s - e).y;
		tymax = (node->bounding.at(3) - e.y) / (s - e).y;
	}
	else
	{
		tymin = (node->bounding.at(3) - e.y) / (s - e).y;
		tymax = (node->bounding.at(2) - e.y) / (s - e).y;
	}

	if (txmin > tymax || tymin > txmax)
	{
		isIntersected = false;
	}

	if (tymin > txmin)
	{
		txmin = tymin;
	}

	if (tymax < txmax)
	{
		txmax = tymax;
	}

	if ((s - e).z >= 0)
	{
		tzmin = (node->bounding.at(4) - e.z) / (s - e).z;
		tzmax = (node->bounding.at(5) - e.z) / (s - e).z;
	}
	else
	{
		tzmin = (node->bounding.at(5) - e.z) / (s - e).z;
		tzmax = (node->bounding.at(4) - e.z) / (s - e).z;
	}

	if (txmin > tzmax || tzmin > txmax)
	{
		isIntersected = false;
	}

	if (tzmin > txmin)
	{
		txmin = tzmin;
	}

	if (tzmax < txmax)
	{
		txmax = tzmax;
	}

	if (isIntersected) // there is an intersection between the ray and the node
	{
		if (node->children.size() != 0) // has children, find leaves
		{
			for (int i = 0; i < node->children.size(); i++)
			{
				ray_box_intersection(e, s, node->children.at(i), objects_to_for_hit_testing, pick);
			}
		}
		else // this is a leaf
		{
			for (int i = 0; i < node->shapes_contained.size(); i++)
			{
				bool ifExsit = false;
				for (int j = 0; j < objects_to_for_hit_testing.size(); j++)
				{
					if (objects_to_for_hit_testing.at(j) == node->shapes_contained.at(i))
					{
						ifExsit = true;
					}
				}
				if ( !ifExsit )
				{
					objects_to_for_hit_testing.push_back(node->shapes_contained.at(i));
				}
			}
			for (int i = 0; i < listOfPlanes.size(); i++)
			{
				objects_to_for_hit_testing.push_back(listOfPlanes.at(i));
			}
			if (pick)
			{
				std::cout << "Ray hits a box: " << std::endl;
				std::cout << "	Box's bounding: " << std::endl;
				std::cout << "		x range from: " << node->bounding.at(0) << " to " << node->bounding.at(1) << std::endl;
				std::cout << "		y range from: " << node->bounding.at(2) << " to " << node->bounding.at(3) << std::endl;
				std::cout << "		z range from: " << node->bounding.at(5) << " to " << node->bounding.at(4) << std::endl;

				std::cout << "	Objects in the box: " << std::endl;
				for (int i = 0; i < node->shapes_contained.size(); i++)
				{
					std::cout << "		No. " << i << ": " << std::endl;
					std::cout << "		Type: " << node->shapes_contained.at(i)->type << std::endl;
					std::cout << "		Position: " << node->shapes_contained.at(i)->position.x << ", " << node->shapes_contained.at(i)->position.y << ", " << node->shapes_contained.at(i)->position.z << std::endl;
					std::cout << "		radius: " << node->shapes_contained.at(i)->radius << std::endl;
				}
			}
		}
	}
	else
	{
		for (int i = 0; i < listOfPlanes.size(); i++)
		{
			objects_to_for_hit_testing.push_back(listOfPlanes.at(i));
		}
	}
}