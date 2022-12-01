#define SCRWIDTH 2000 //1280
#define SCRHEIGHT 720

#define MAX_BOUNCE 10
#define EPSILON 0.0001f

#define SPHERE		0
#define PLANE		1
#define CUBE		2
#define QUAD		3
#define TRIANGLE	4

typedef struct Ray
{
	float4 O, D, rD, N, intensity;
	float t;
	// Index of primitive
	int primIdx, bounces;
	bool inside;
} Ray;

typedef struct Primitive
{
	int objType;
	// Index of object in respective list
	int objIdx;
	int matIdx;
} Primitive;

typedef struct Material
{
	float4 color;
	float specular, n1, n2;
	bool isDieletric;
	int texIdx;
	int texW, texH;
} Material;

typedef struct Sphere
{
	float4 pos;
	float r2, invr;
} Sphere;

typedef struct Plane
{
	float4 N;
	float d;
} Plane;

typedef struct Triangle
{
	float4 v0, v1, v2;
	float2 uv0, uv1, uv2;
	float4 N;
	float u, v, w; // barycenter, is calculated upon intersection
} Triangle;

typedef struct Light
{
	float4 pos, color;
	float strength;
	// Not used for point lights
	int primIdx;
} Light;