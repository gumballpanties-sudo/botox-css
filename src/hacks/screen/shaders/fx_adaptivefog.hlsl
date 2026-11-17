// Adaptive fog (reshade OtisFX AdaptiveFog.fx), one ps_3_0 pass. Same text as botox
// hacks/visuals/screen/depth_view.cpp k_fog_shader. SOURCE OF RECORD for fx_adaptivefog.h.
//
// Rebuild after ANY edit here:
//   fxc /nologo /T ps_3_0 /E main /Fh src/hacks/screen/shaders/fx_adaptivefog.h /Vn k_fxAdaptiveFogBytes src/hacks/screen/shaders/fx_adaptivefog.hlsl
//
//   s0 = resolved depth (INTZ, point)   s1 = copy of the frame (linear, like reshade's BackBuffer)
//   c0 = 1/w, 1/h, bloom width, unused
//   c1 = fog colour rgb, max fog factor
//   c2 = fog curve, fog start, bloom gain (power+4)/(25-(power-threshold*5)), unused
//   c3 = zNear, zFar (this frame's view setup), unused, unused
//
// Otis's two passes fused: the blend only reads the bloom target at its own pixel, so the bloom
// is done inline and only where fog lands (f 0 = 2 fetches, not 27). Tap weight
// sqrt(CurDistance/MaxDistance) = sqrt((x*x+y*y)/8): bloom width cancels, fxc folds it.
// lin() = depth view's map independent reshade lin (far 1000 over the default 7..28377.9 range).

sampler2D s_depth:register(s0);
sampler2D s_frame:register(s1);
float4 c0:register(c0);
float4 c1:register(c1);
float4 c2:register(c2);
float4 c3:register(c3);
float lin(float2 uv){
	float d=tex2Dlod(s_depth,float4(uv,0,0)).r;
	if(d>=1.0)return 1.0;
	float z=c3.x*c3.y/(c3.y-d*(c3.y-c3.x));
	float r=saturate(28377.9*(z-7.0)/(z*28370.9));
	return r/(1000.0-r*999.0);
}
float4 main(float2 uv:TEXCOORD0):COLOR0{
	float4 col=tex2Dlod(s_frame,float4(uv,0,0));
	float f=clamp(saturate(lin(uv)-c2.y)*c2.x,0.0,c1.w);
	[branch]if(f<=0.0)return col;
	[branch]if(f>=1.0)return float4(c1.rgb,col.a);
	float2 tap=uv*c0.xy*c0.z;
	float3 sum=0;
	[unroll]for(int i=0;i<5;i++){
		float bx=2.5-i;
		[unroll]for(int j=0;j<5;j++){
			float by=-2.5+j;
			float3 t=tex2Dlod(s_frame,float4(uv+float2(bx,by)*tap,0,0)).rgb;
			sum+=lerp(t,col.rgb,sqrt((bx*bx+by*by)/8.0));
		}
	}
	float3 bloom=saturate(lerp(col.rgb,sum*c2.z,dot(col.rgb,float3(0.299,0.587,0.114))));
	return float4(lerp(col.rgb,lerp(bloom,c1.rgb,f),f),col.a);
}
