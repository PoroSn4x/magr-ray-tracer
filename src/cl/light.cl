
float4 getDiffuseShading( Light* light, float dot, float r )
{
    return dot * (1 / (r * r)) * light->strength * light->color;
}

bool shootShadowRay( Ray* ray, float d )
{
    for(int i = 0; i < nPrimitives; i++)
    {
        intersect(i, primitives + i, ray);
        if(ray->t < d - EPSILON)
            return false;
    }
    //printf("shoot");
    return true;
}

float4 handleShadowRay( Ray* ray, Light* light )
{
    float4 dir = intersectionPoint(ray) - light->pos;
    float dotP = dot(ray->N, -dir);
    if(dotP > 0)
    {
        //printf("handle");
        // Shoot a shadow ray into the scene and check if anything obstructs it
        Ray shadowRay = initRay(light->pos, dir);
        float dist = length(dir) - EPSILON;
        if(shootShadowRay(&shadowRay, dist))
        {
            return getDiffuseShading(light, dotP, dist);
        }
    } 
    return (float4)(0);
}
