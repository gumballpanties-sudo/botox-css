// Emphasize (reshade Emphasize.fx) + show depth (reshade DisplayDepth.fx), one ps_3_0 pass.
// Copied 1:1 from botox hacks/visuals/screen/depth_view.cpp k_shader. SOURCE OF RECORD for
// fx_depthview.h.
//
// Rebuild after ANY edit here:
//   fxc /nologo /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_depthview.h /Vn k_fxDepthViewBytes src/hacks/screen/shaders/fx_depthview.hlsl
//
//   s0 = resolved depth (INTZ, point)   s1 = copy of the frame
//   c0 = emphasize on, show depth on, 1/w, 1/h
//   c1 = focus depth, focus range, focus edge, spherical
//   c2 = sphere fov, sphere x, sphere y, effect factor
//   c3 = blend colour rgb, blend factor
//   c4 = present type, blend into image, live preview, unused
//   c5 = upside down, reversed, logarithmic, far plane
//   c6 = scale x, scale y, offset x px, offset y px
//   c7 = multiplier, width, height, unused
//   c8 = zNear, zFar (this frame's CViewSetup), unused, unused
//
// DIH: lin() is botox's reshade lin (far plane 1000) of RAW depth -- and raw depth moves with
// the map's zFar (env_fog_controller farz, r_mapextents), so one slider value hit a different
// distance on every map. Now: unproject with the real z range, re-project through TF2's
// default one (VIEW_NEARZ 7, 16384*sqrt3 = 28377.9, view.cpp GetZFar), then lin. Default maps
// read exactly as before; every other map reads like them. Sky (raw 1) stays 1.

sampler2D s_depth:register(s0);
sampler2D s_frame:register(s1);
float4 c0:register(c0);
float4 c1:register(c1);
float4 c2:register(c2);
float4 c3:register(c3);
float4 c4:register(c4);
float4 c5:register(c5);
float4 c6:register(c6);
float4 c7:register(c7);
float4 c8:register(c8);
float lin(float2 uv){
	float d=tex2Dlod(s_depth,float4(uv,0,0)).r;
	if(d>=1.0)return 1.0;
	float z=c8.x*c8.y/(c8.y-d*(c8.y-c8.x));
	float r=saturate(28377.9*(z-7.0)/(z*28370.9));
	return r/(1000.0-r*999.0);
}
float lin_view(float2 uv){
	if(c4.z<0.5)return lin(uv);
	if(c5.x>0.5)uv.y=1.0-uv.y;
	uv/=c6.xy;
	uv.x-=c6.z*c0.z;
	uv.y+=c6.w*c0.w;
	float d=tex2Dlod(s_depth,float4(uv,0,0)).r*c7.x;
	if(c5.z>0.5)d=(exp(d*log(1.01))-1.0)/0.01;
	if(c5.y>0.5)d=1.0-d;
	d/=c5.w-d*(c5.w-1.0);
	return d;
}
float3 normal_at(float2 uv){
	float2 pn=uv-float2(0,c0.w);
	float2 pe=uv+float2(c0.z,0);
	float3 vc=float3(uv-0.5,1)*lin_view(uv);
	float3 vn=float3(pn-0.5,1)*lin_view(pn);
	float3 ve=float3(pe-0.5,1)*lin_view(pe);
	return normalize(cross(vc-vn,vc-ve))*0.5+0.5;
}
float4 main(float2 uv:TEXCOORD0,float2 vpos:VPOS):COLOR0{
	float4 col=tex2Dlod(s_frame,float4(uv,0,0));
	if(c0.x>0.5){
		float sd=lin(uv);
		float sf=c1.x;
		float full=c1.y+c1.z;
		float dd;
		if(c1.w>0.5){
			float2 t=(uv-c2.yz)*c7.yz;
			float fd=sqrt(dot(t,t))*(c2.x/c7.y);
			dd=sqrt(sd*sd+sf*sf-2.0*sd*sf*cos(fd*(2.0*3.1415927/360.0)));
		}else{
			dd=abs(sd-sf);
		}
		float coc=saturate(dd>full?1.0:smoothstep(c1.y,full,dd));
		float g=(col.r+col.g+col.b)/3.0;
		float4 des=float4(g,g,g,coc);
		des=lerp(des,float4(c3.rgb,coc),c3.w);
		col=lerp(col,des,saturate(coc*c2.w));
	}
	if(c0.y>0.5){
		float3 depth=lin_view(uv).xxx;
		float3 n=normal_at(uv);
		float grid=frac(dot(uv,(c7.yz*float2(1.0/16.0,10.0/36.0))+0.25));
		float ds=0.25*(1.0/(pow(2.0,8.0)-1.0));
		float3 dsr=float3(ds,-ds,ds);
		dsr=lerp(2.0*dsr,-2.0*dsr,grid);
		depth+=dsr;
		float3 c=depth;
		if(c4.x>0.5&&c4.x<1.5)c=n;
		if(c4.x>1.5)c=lerp(n,depth,step(c7.y*0.5,vpos.x));
		if(c4.y>0.5){
			float3 o=col.rgb;
			c=lerp(2.0*c*o,1.0-2.0*(1.0-c)*(1.0-o),max(c.r,max(c.g,c.b))<0.5?0.0:1.0);
		}
		col.rgb=c;
	}
	return col;
}
