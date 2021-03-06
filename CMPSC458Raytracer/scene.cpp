#include "scene.h"

scene::scene(const char* filename)
{
	parse(filename);
	cout<<endl<<"background  "<<bgColor;
	cout<<endl<<"ambient     "<<ambLight;
	cout<<endl<<"eye         "<<eye;
	cout<<endl<<"lookAt      "<<lookAt;
	cout<<endl<<"up          "<<up;
	cout<<endl<<"fovy        "<<fovy;
	cout<<endl;
}

bool refract(Vec3f d, Vec3f n, float refI, Vec3f *t)
{
	float disc = 1 - (refI * refI * (1 - pow(d.Dot3(n), 2)));
	if(disc < 0)
		return false;

	*t = (d - n * d.Dot3(n)) * refI - n * sqrt(disc);
	t->Normalize();

	return true;
}

Vec3f scene::rayTrace(Vec3f eye, Vec3f dir, int recurseDepth, rtObject *skip_obj)
{
	//start with black, add color as we go
	Vec3f answer(0,0,0);

	//test for intersection against all our objects
	float dist = myObjGroup->testIntersections(eye, dir, skip_obj);

	//if we saw nothing, return the background color of our scene
	if (dist==MAX_DIST)
		return bgColor;

	rtObject *closest_obj = myObjGroup->getClosest();
	Vec3f closest_inter = eye + dir * dist;

	//get the material index and normal vector(at the point we saw) of the object we saw
	int matIndex = closest_obj->getMatIndex();
	Vec3f normal = closest_obj->getNormal(eye, dir);
	
	//determine texture color
	Vec3f textureColor;
	if (myMaterials.at(matIndex).texture==NULL)
	{
		textureColor.Set(1,1,1);
	}
	else
	{
		//if there is a texture image, ask the object for the image coordinates (between 0 and 1)
		Vec3f coords = closest_obj->getTextureCoords(eye, dir);

		//get the color from that image location
		textureColor.Set(
			PIC_PIXEL(myMaterials.at(matIndex).texture,(int)(myMaterials.at(matIndex).texture->nx*coords.x()),(int)(myMaterials.at(matIndex).texture->ny*coords.y()),0),
			PIC_PIXEL(myMaterials.at(matIndex).texture,(int)(myMaterials.at(matIndex).texture->nx*coords.x()),(int)(myMaterials.at(matIndex).texture->ny*coords.y()),1),
			PIC_PIXEL(myMaterials.at(matIndex).texture,(int)(myMaterials.at(matIndex).texture->nx*coords.x()),(int)(myMaterials.at(matIndex).texture->ny*coords.y()),2));
		textureColor = textureColor*(1/255.0);
	}

	//add diffuse color times ambient light to our answer
	answer += multiplyColorVectors(myMaterials.at(matIndex).diffuseCol, ambLight);

	//iterate through all lights
	for(vector<light>::iterator li = myLights.begin(); li != myLights.end(); li++)
	{
		Vec3f light = li->position - closest_inter;
		light.Normalize();

		float t = myObjGroup->testIntersections(closest_inter, light, closest_obj);
		
		Vec3f accumLightDecay(1, 1, 1);
		int lightpass = 0;


		while(myObjGroup->getClosest() != closest_obj && lightpass < 3 && t < MAX_DIST)
		{
			accumLightDecay = multiplyColorVectors(myMaterials.at(myObjGroup->getClosest()->getMatIndex()).transparentCol, accumLightDecay);
			lightpass++;
			t = myObjGroup->testIntersections(li->position + light * t, light, myObjGroup->getClosest());
		}

		if(lightpass != 3 && t == MAX_DIST)
		{
			Vec3f lambert = multiplyColorVectors(myMaterials.at(matIndex).diffuseCol, li->color * max(0.0f, light.Dot3(normal)));
			lambert = multiplyColorVectors(lambert, accumLightDecay);
			answer += lambert;

			Vec3f h = light - dir;
			h.Normalize();

			Vec3f phong = myMaterials.at(matIndex).specularCol * pow(normal.Dot3(h), myMaterials.at(matIndex).shininess);
			phong = multiplyColorVectors(phong, accumLightDecay);
			answer += phong;
		}
	}

	int recurseLimit = 3;

	//put a limit on the depth of recursion
	if (recurseDepth<recurseLimit)
	{
		Vec3f recurseCol;

		//reflect our view across the normal
		Vec3f reflect = dir - (normal * dir.Dot3(normal) * 2);
		reflect.Normalize();

		//recusively raytrace from the surface point along the reflected view
		//add the color seen times the reflective color
		
		recurseCol = rayTrace(closest_inter, reflect, recurseDepth+1, closest_obj);
		
		if(recurseCol != bgColor)
		{
			recurseCol = multiplyColorVectors(recurseCol, myMaterials.at(matIndex).reflectiveCol);
			answer += recurseCol;
		}


		// test for refraction
		float refI = myMaterials.at(matIndex).refractionIndex;
		bool refracts;
		Vec3f t;

		//if going into material (dot prod of dir and normal is negative), bend toward normal
		if(dir.Dot3(normal) < 0)
			refracts = refract(dir, normal, refI, &t); 
		//else, bend away
		else
			refracts = refract(dir, normal * -1, 1 / refI, &t); 

		//recursively raytrace from the other side of the object along the new direction
		if(refracts) recurseCol = rayTrace(closest_inter, t, recurseDepth + 1, closest_obj);
		//add the color seen times the transparent color
		answer += multiplyColorVectors(recurseCol, myMaterials.at(matIndex).transparentCol);

	}

	//multiply whatever color we have found by the texture color
	answer=multiplyColorVectors(answer,textureColor);

	return answer;
}

Vec3f scene::multiplyColorVectors(Vec3f a, Vec3f b) //multiplies the terms of two color-holding vec3f vars
{
	Vec3f result(a.x() * b.x(), a.y() * b.y(), a.z() * b.z());
	return result;
}

Vec3f scene::getEye()
{
	return eye;
}

Vec3f scene::getLookAt()
{
	return lookAt;
}

Vec3f scene::getUp()
{
	return up;
}

float scene::getFovy()
{
	return fovy;
}

void scene::parse(const char *filename)
{
	//some default values in case parsing fails
	myObjGroup = NULL;
	bgColor = Vec3f(0.5,0.5,0.5);
	ambLight = Vec3f(0.5,0.5,0.5);
	eye = Vec3f(0,0,0);
	lookAt = Vec3f(0,0,-1);
	up = Vec3f(0,1,0);
	fovy = 45;
	file = NULL;
	curline = 1;

	//the file-extension needs to be "ray"
	assert(filename != NULL);
	const char *ext = &filename[strlen(filename)-4];
	assert(!strcmp(ext,".ray"));
	fopen_s(&file, filename, "r");
	assert(file != NULL);

	char token[MAX_PARSER_TOKEN_LENGTH];

	//prime the parser pipe
	parse_char = fgetc(file);

	while (getToken(token))
	{
		if(!strcmp(token, "Background"))
			parseBackground();
		else if(!strcmp(token, "Camera"))
			parseCamera();
		else if(!strcmp(token, "Materials"))
			parseMaterials();
		else if(!strcmp(token, "Group"))
			myObjGroup = parseGroup();
		else if(!strcmp(token, "Lights"))
			parseLights();
		else
		{
			cout<<"Unknown token in parseFile: '"<<token<<
				"' at input line "<<curline<<"\n";
			exit(-1);
		}
	}
}

/* Parse the "Camera" token */
void scene::parseCamera()
{
	char token[MAX_PARSER_TOKEN_LENGTH];
	float x,y,z;
	getToken(token); assert (!strcmp(token, "{"));

	//get the eye, center, and up vectors (similar to gluLookAt)
	getToken(token); assert (!strcmp(token, "eye"));
	x = readFloat();
	y = readFloat();
	z = readFloat();
	eye.Set(x,y,z);

	getToken(token); assert (!strcmp(token, "lookAt"));
	x = readFloat();
	y = readFloat();
	z = readFloat();
	lookAt.Set(x,y,z);

	getToken(token); assert (!strcmp(token, "up"));
	x = readFloat();
	y = readFloat();
	z = readFloat();
	up.Set(x,y,z);

	getToken(token); assert (!strcmp(token, "fovy"));
	fovy = readFloat();

	getToken(token); assert (!strcmp(token, "}"));
}

/* Parses the "Background" token */
void scene::parseBackground()
{
	char token[MAX_PARSER_TOKEN_LENGTH];

	getToken(token); assert (!strcmp(token, "{"));
	while (1)
	{
		getToken(token);
		if (!strcmp(token, "}"))
		{
			break;
		}
		else if (!strcmp(token, "color"))
		{
			bgColor = readVec3f();
		}
		else if (!strcmp(token, "ambientLight"))
		{
			ambLight = readVec3f();
		}
		else
		{
			cout<< "Unknown token in parseBackground: "<<token<<"\n";
			assert(0);
		}
	}
}

/* Parses the "Group" token */
rtObjGroup* scene::parseGroup()
{
	char token[MAX_PARSER_TOKEN_LENGTH];


	getToken(token);
	assert (!strcmp(token, "{"));

	/**********************************************/
	/* Instantiate the group object               */
	/**********************************************/
	rtObjGroup *answer     = new rtObjGroup();

	bool working=true;
	while (working)
	{
		getToken(token);
		if (!strcmp(token,"}"))
		{
			working=false;
		}
		else
		{
			if (!strcmp(token, "Sphere"))
			{
				sphere *sceneElem = parseSphere();
				assert(sceneElem != NULL);
				answer->addObj(sceneElem);
			}
			else if (!strcmp(token, "Triangle"))
			{
				triangle *sceneElem = parseTriangle();
				assert(sceneElem != NULL);
				answer->addObj(sceneElem);
			}
			else
			{
				cout << "Unknown token in Group: '" << token << "' at line "
					<< curline << "\n";
				exit(0);
			}
		}
	}

	/* Return the group */
	return answer;
}

/* Parse the "Sphere" token */
sphere* scene::parseSphere()
{
	char token[MAX_PARSER_TOKEN_LENGTH];

	getToken(token); assert (!strcmp(token, "{"));
	getToken(token); assert (!strcmp(token, "materialIndex"));
	int sphere_material_index = readInt();
	getToken(token); assert (!strcmp(token, "center"));
	Vec3f center = readVec3f();
	getToken(token); assert (!strcmp(token, "radius"));
	float radius = readFloat();
	getToken(token); assert (!strcmp(token, "}"));

	/**********************************************/
	/* The call to your own constructor goes here */
	/**********************************************/
	return new sphere(center,radius,sphere_material_index,this);
}

/* Parse out the "Triangle" token */
triangle* scene::parseTriangle()
{
	char token[MAX_PARSER_TOKEN_LENGTH];
	getToken(token); assert (!strcmp(token, "{"));

	/* Parse out vertex information */
	getToken(token); assert (!strcmp(token, "vertex0"));
	Vec3f v0 = readVec3f();
	getToken(token); assert (!strcmp(token, "vertex1"));
	Vec3f v1 = readVec3f();
	getToken(token); assert (!strcmp(token, "vertex2"));
	Vec3f v2 = readVec3f();
	getToken(token); assert (!strcmp(token, "tex_xy_0"));
	float x0 = 0;
	float y0 = 0;
	x0 = readFloat();
	y0 = readFloat();
	getToken(token); assert (!strcmp(token, "tex_xy_1"));
	float x1 = 0;
	float y1 = 0;
	x1 = readFloat();
	y1 = readFloat();
	getToken(token); assert (!strcmp(token, "tex_xy_2"));
	float x2 = 0;
	float y2 = 0;
	x2 = readFloat();
	y2 = readFloat();
	getToken(token); assert (!strcmp(token, "materialIndex"));
	int mat = readInt();

	getToken(token); assert (!strcmp(token, "}"));

	/**********************************************/
	/* The call to your own constructor goes here */
	/**********************************************/
	return new triangle(v0,v1,v2,x0,x1,x2,y0,y1,y2,mat,this);
}

/* Parse the "Materials" token */
void scene::parseMaterials()
{
	char token[MAX_PARSER_TOKEN_LENGTH];
	char texname[MAX_PARSER_TOKEN_LENGTH];
	getToken(token); assert (!strcmp(token, "{"));

	/* Loop over each Material */
	bool working=true;
	while (working)
	{
		getToken(token);
		if (!strcmp(token, "}"))
		{
			working = false;
		}
		else if (!strcmp(token, "Material"))
		{
			getToken(token); assert (!strcmp(token, "{"));
			texname[0] = '\0';
			Vec3f diffuseColor(1,1,1);
			Vec3f specularColor(0,0,0);
			float shininess = 1;
			Vec3f transparentColor(0,0,0);
			Vec3f reflectiveColor(0,0,0);
			float indexOfRefraction = 1;

			while (1)
			{
				getToken(token);
				if (!strcmp(token, "textureFilename"))
				{
					getToken(token);
					strcpy(texname, token);
				}
				else if (!strcmp(token, "diffuseColor"))
					diffuseColor = readVec3f();
				else if (!strcmp(token, "specularColor"))
					specularColor = readVec3f();
				else if  (!strcmp(token, "shininess"))
					shininess = readFloat();
				else if (!strcmp(token, "transparentColor"))
					transparentColor = readVec3f();
				else if (!strcmp(token, "reflectiveColor"))
					reflectiveColor = readVec3f();
				else if (!strcmp(token, "indexOfRefraction"))
					indexOfRefraction = readFloat();
				else
				{
					assert (!strcmp(token, "}"));
					break;
				}
			}

			material temp;
			temp.diffuseCol=diffuseColor;
			temp.specularCol=specularColor;
			temp.shininess=shininess;
			temp.transparentCol=transparentColor;
			temp.reflectiveCol=reflectiveColor;
			temp.refractionIndex=indexOfRefraction;
			if(strcmp(texname, "NULL"))
				temp.texture=jpeg_read(texname, NULL);
			else
				temp.texture=NULL;

			myMaterials.push_back(temp);

		}
	}
}

void scene::parseLights()
{
	char token[MAX_PARSER_TOKEN_LENGTH];
	char texname[MAX_PARSER_TOKEN_LENGTH];
	getToken(token); assert (!strcmp(token, "{"));

	/* Loop over each Material */
	bool working=true;
	while (working)
	{
		getToken(token);
		if (!strcmp(token, "}"))
		{
			working = false;
		}
		else if (!strcmp(token, "Light"))
		{
			getToken(token); assert (!strcmp(token, "{"));
			texname[0] = '\0';
			Vec3f position(0,0,0);
			Vec3f color(1,1,1);

			while (1)
			{
				getToken(token);
				if (!strcmp(token, "position"))
					position = readVec3f();
				else if (!strcmp(token, "color"))
					color = readVec3f();
				else
				{
					assert (!strcmp(token, "}"));
					break;
				}
			}

			/**********************************************/
			/* The call to your own constructor goes here */
			/**********************************************/
			light temp;
			temp.position=position;
			temp.color=color;

			myLights.push_back(temp);

		}
	}
}

/* consume whitespace */
void scene::eatWhitespace(void)
{
	bool working = true;

	do{
		while(isspace(parse_char))
		{
			if(parse_char == '\n')
			{
				curline++;
			}
			parse_char = fgetc(file);
		}

		if('#'==parse_char)
		{
			/* this is a comment... eat until end of line */
			while(parse_char != '\n')
			{
				parse_char = fgetc(file);
			}

			curline++;
		}
		else
		{
			working = false;
		}

	} while(working);
}

/* Parse out a single token */
int scene::getToken(char token[MAX_PARSER_TOKEN_LENGTH])
{
	int idx=0;

	assert (file != NULL);
	eatWhitespace();

	if(parse_char == EOF)
	{
		token[0]='\0';
		return 0;
	}
	while((!(isspace(parse_char))) && (parse_char != EOF))
	{
		token[idx]=parse_char;
		idx++;
		parse_char = fgetc(file);
	}

	token[idx] = '\0';
	return 1;
}

/* Reads in a 3-vector */
Vec3f scene::readVec3f()
{
	float x,y,z;

	x=readFloat();
	y=readFloat();
	z=readFloat();

	return Vec3f(x,y,z);
}

/* Reads in a single float */
float scene::readFloat()
{
	float answer;
	char buf[MAX_PARSER_TOKEN_LENGTH];

	if(!getToken(buf))
	{
		cout<< "Error trying to read 1 float (EOF?)\n";
		assert (0);
	}

	int count = sscanf(buf,"%f",&answer);
	if (count != 1)
	{
		cout<< "Error trying to read 1 float\n";
		assert (0);

	}
	return answer;
}

/* Reads in a single int */
int scene::readInt()
{
	int answer;
	char buf[MAX_PARSER_TOKEN_LENGTH];

	if(!getToken(buf))
	{
		cout<< "Error trying to read 1 int (EOF?)\n";
		assert (0);
	}

	int count = sscanf(buf,"%d",&answer);
	if (count != 1)
	{
		cout<< "Error trying to read 1 int\n";
		assert (0);

	}
	return answer;
}