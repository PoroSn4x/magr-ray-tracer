#ifndef __RAY_CL
#define __RAY_CL

Ray initRay( float4 O, float4 D )
{
	Ray ray;
	ray.O = O;
	ray.D = D;
	ray.rD = 1 / D;
	ray.N = (float4)(0);
	ray.I = (float4)(0);
	ray.intensity = (float4)(1);
	ray.t = 1e30f;
	ray.primIdx = -1;
	ray.bounces = 0;
	ray.inside = false;
	ray.lastSpecular = false;
	return ray;
}

Ray reflect( Ray* ray )
{
	float4 reflected = ray->D - 2.f * ray->N * dot( ray->N, ray->D );
	float4 origin = ray->I + reflected * 2 * EPSILON;
	Ray reflectRay = initRay( origin, reflected );
	reflectRay.intensity = ray->intensity;
	reflectRay.bounces = ray->bounces + 1;
	return reflectRay;
}

Ray transmit( Ray* ray, float4 T )
{
	float4 origin = ray->I + T * EPSILON;
	Ray tRay = initRay( origin, T );
	tRay.intensity = ray->intensity;
	tRay.bounces = ray->bounces + 1;
	tRay.inside = !ray->inside;
	return tRay;
}

void intersectionPoint( Ray* ray )
{
	ray->I = ray->O + ray->t * ray->D;
}

float4 randomRayHemisphere( float4 N, uint* seed )
{
    // random point from (-1,-1,-1) to (1,1,1)
    float4 p = randomFloat3( seed ) * 2 - 1;
    // reject if outside unit sphere
    while (p.x * p.x + p.y * p.y + p.z * p.z > 1)
        p = randomFloat3( seed ) * 2 - 1;
    p = normalize( p );
    // flip if in wrong half
    return (dot( N, p ) < 0) ? -p : p;
}

// use rejection sampling
float4 cosineWeightedRayHemisphere( float4 N, uint* seed )
{
	// random point from (-1,-1,-1) to (1,1,1)
	float4 p = randomFloat3( seed ) * 2 - 1;

	// reject if outside unit sphere
	while( p.x*p.x + p.y*p.y+p.z*p.z > 1 )
		p = randomFloat3( seed ) * 2 - 1;

	// normalize so it becomes a point on the boundary of the unit sphere
	p = normalize( p );
	return normalize( N + p );

}
#endif // __RAY_CL