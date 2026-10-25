//! Noqeri shader GPU EXECUTION evidence: offscreen 2D/3D WebGPU via Vulkan.
//! Ref WGSL and Noqeri-emitted WGSL use the same GPU adapter. Pipeline creation
//! and PNG readback are OUTSIDE the timed frame loop.
//! This intentionally does NOT claim hardware GPU if adapter is llvmpipe.

use std::{borrow::Cow, collections::BTreeSet, env, error::Error, fs, path::{Path, PathBuf}, sync::mpsc, time::Instant};
use serde::Serialize;
use wgpu::util::DeviceExt;

type Result<T> = std::result::Result<T, Box<dyn Error>>;

const WARMUPS: usize = 4;
const SAMPLES: usize = 25;
const FORMAT: wgpu::TextureFormat = wgpu::TextureFormat::Rgba8Unorm;

const SCREEN_VERTEX: &str = r#"
@vertex
fn fullscreen(@builtin(vertex_index) index: u32) -> @builtin(position) vec4<f32> {
  let positions = array<vec2<f32>, 3>(
    vec2<f32>(-1.0, -1.0),
    vec2<f32>(3.0, -1.0),
    vec2<f32>(-1.0, 3.0)
  );
  return vec4<f32>(positions[index], 0.0, 1.0);
}
"#;

#[derive(Serialize)]
struct Timings {
    median_ms: f64,
    p95_ms: f64,
    min_ms: f64,
    max_ms: f64,
    samples_ms: Vec<f64>,
}

#[derive(Serialize)]
struct Comparison {
    max_abs_channel_difference: u8,
    changed_channels_over_2: usize,
    sample_pixels: usize,
    colored_pixels: usize,
    distinct_color_buckets: usize,
}

#[derive(Serialize)]
struct SceneRecord {
    scene: String,
    width: u32,
    height: u32,
    source: String,
    nq_wgsl: Vec<String>,
    reference_wgsl: Vec<String>,
    nq_png: String,
    reference_png: String,
    noqeri: Timings,
    reference: Timings,
    speed_ratio_noqeri_to_reference: f64,
    comparison: Comparison,
}

struct Scene {
    pipelines: Vec<wgpu::RenderPipeline>,
    vertex_buffer: Option<wgpu::Buffer>,
    width: u32,
    height: u32,
    kind: String,
    depth: Option<wgpu::Texture>,
    texture: wgpu::Texture,
    _color_view: wgpu::TextureView,
    _depth_view: Option<wgpu::TextureView>,
}
fn quantile_stats(mut samples: Vec<f64>) -> Timings {
    samples.sort_by(|a,b|a.partial_cmp(b).unwrap());
    Timings {
        median_ms: samples[samples.len()/2],
        p95_ms: samples[(((samples.len() as f64)*0.95).ceil() as usize).saturating_sub(1)],
        min_ms: samples[0], max_ms: *samples.last().unwrap(), samples_ms: samples
    }
}
fn shader_module(device: &wgpu::Device, path: &Path) -> Result<wgpu::ShaderModule> {
    let s=fs::read_to_string(path)?;
    Ok(device.create_shader_module(wgpu::ShaderModuleDescriptor {
        label: Some(path.to_str().unwrap_or("shader")),
        source: wgpu::ShaderSource::Wgsl(Cow::Owned(s))
    }))
}
fn render_pipeline(device: &wgpu::Device,
                   vertex: &wgpu::ShaderModule, fragment: &wgpu::ShaderModule,
                   mesh: bool, depth: bool) -> wgpu::RenderPipeline {
    let attrs=[wgpu::VertexAttribute {
        offset:0, shader_location:0, format:wgpu::VertexFormat::Float32x4
    }];
    let layouts=[wgpu::VertexBufferLayout {
        array_stride:16, step_mode: wgpu::VertexStepMode::Vertex, attributes:&attrs
    }];
    device.create_render_pipeline(&wgpu::RenderPipelineDescriptor {
        label:Some("Noqeri vs direct WGSL GPU render pipeline"),
        layout:None,
        vertex:wgpu::VertexState {
            module:vertex, entry_point:"main", buffers: if mesh {&layouts} else {&[]}
        },
        fragment:Some(wgpu::FragmentState {
            module:fragment, entry_point:"main",
            targets:&[Some(wgpu::ColorTargetState{
                format:FORMAT, blend:None, write_mask:wgpu::ColorWrites::ALL
            })]
        }),
        primitive:wgpu::PrimitiveState {
            topology:wgpu::PrimitiveTopology::TriangleList,
            strip_index_format:None,
            front_face:wgpu::FrontFace::Ccw,
            cull_mode:None, polygon_mode:wgpu::PolygonMode::Fill,
            unclipped_depth:false, conservative:false,
        },
        depth_stencil:if depth {Some(wgpu::DepthStencilState {
            format:wgpu::TextureFormat::Depth24Plus,
            depth_write_enabled:true,
            depth_compare:wgpu::CompareFunction::Less,
            stencil:wgpu::StencilState::default(),
            bias:wgpu::DepthBiasState::default(),
        })} else {None},
        multisample:wgpu::MultisampleState::default(),
        multiview:None,
    })
}

fn cube_vertices() -> Vec<f32> {
    let corners=[
        [-1.0f32,-1.0,-1.0], [1.0,-1.0,-1.0],
        [1.0,1.0,-1.0], [-1.0,1.0,-1.0],
        [-1.0,-1.0,1.0], [1.0,-1.0,1.0],
        [1.0,1.0,1.0], [-1.0,1.0,1.0]
    ];
    let faces=[[0,1,2,3], [4,5,6,7], [0,4,7,3],
               [1,5,6,2], [3,2,6,7], [0,1,5,4]];
    let mut positions=Vec::new();
    for face in faces {
        for id in [face[0],face[1],face[2],face[0],face[2],face[3]] {
            let p=corners[id];
            let ay=0.70f32;let ax=0.48f32;
            let rx=p[0]*ay.cos()+p[2]*ay.sin();
            let rz=-p[0]*ay.sin()+p[2]*ay.cos();
            let ry=p[1]*ax.cos()-rz*ax.sin();
            let rz2=p[1]*ax.sin()+rz*ax.cos();
            let z=4.5+rz2;
            let x=1.65*rx/z;
            let y=1.65*ry/z;
            let depth= z / (z+1.0);
            positions.extend_from_slice(&[x,y,depth,1.0]);
        }
    }
    positions
}

fn mesh_positions(kind:&str) -> Vec<f32> {
    match kind {
        "depth" => vec![
          -0.85,-0.78,0.70,1.0, 0.85,-0.78,0.70,1.0, 0.0,0.85,0.70,1.0,
          -0.50,-0.30,0.20,1.0, 0.50,-0.30,0.20,1.0, 0.0,0.56,0.20,1.0,
        ],
        "cube" => cube_vertices(),
        _ => unreachable!()
    }
}

fn scene_init(device:&wgpu::Device, root:&Path,
              kind:&str, width:u32,height:u32, implementation:&str) -> Result<Scene> {
    let shaders=root.join(implementation);
    let mesh=kind=="cube"||kind=="depth";
    let vmodule=if mesh {
        shader_module(device,&shaders.join("vertex3d.wgsl"))?
    }else {
        device.create_shader_module(wgpu::ShaderModuleDescriptor{
            label:Some("same fullscreen vertex shader for both tests"),
            source:wgpu::ShaderSource::Wgsl(Cow::Borrowed(SCREEN_VERTEX))
        })
    };
    let fragment_names:Vec<&str> = match kind {
        "gradient"=>vec!["color_gradient"],
        "posterize"=>vec!["posterize_fragment"],
        "vector"=>vec!["vector_fragment"],
        "depth"=>vec!["fragment3d","fragment_near"],
        "cube"=>vec!["face0","face1","face2","face3","face4","face5"],
        _=>return Err(format!("unsupported scene: {kind}").into())
    };
    let mut pipelines=Vec::new();
    for name in fragment_names {
        let frag=shader_module(device,&shaders.join(format!("{name}.wgsl")))?;
        pipelines.push(render_pipeline(device,&vmodule,&frag,mesh,mesh));
    }
    let vertex_buffer=if mesh {
        let verts=mesh_positions(kind);
        Some(device.create_buffer_init(&wgpu::util::BufferInitDescriptor{
            label:Some("Noqeri 3D clip-space mesh"),
            contents:bytemuck::cast_slice(&verts),
            usage:wgpu::BufferUsages::VERTEX
        }))
    } else {None};
    let texture=device.create_texture(&wgpu::TextureDescriptor {
        label:Some("Noqeri offscreen GPU RGBA8 2D/3D image"),
        size:wgpu::Extent3d {width,height,depth_or_array_layers:1},
        mip_level_count:1,sample_count:1,dimension:wgpu::TextureDimension::D2,
        format:FORMAT,usage:wgpu::TextureUsages::RENDER_ATTACHMENT|wgpu::TextureUsages::COPY_SRC,
        view_formats:&[]
    });
    let color_view=texture.create_view(&Default::default());
    let depth=if mesh {
        Some(device.create_texture(&wgpu::TextureDescriptor{
            label:Some("real Vulkan depth24plus attachment"),
            size:wgpu::Extent3d {width,height,depth_or_array_layers:1},
            mip_level_count:1,sample_count:1,dimension:wgpu::TextureDimension::D2,
            format:wgpu::TextureFormat::Depth24Plus,
            usage:wgpu::TextureUsages::RENDER_ATTACHMENT,view_formats:&[]
        }))
    }else {None};
    let depth_view=depth.as_ref().map(|d|d.create_view(&Default::default()));
    Ok(Scene{
        pipelines,vertex_buffer,width,height,kind:kind.to_string(),
        depth,texture,_color_view:color_view,_depth_view:depth_view
    })
}

fn frame(device:&wgpu::Device,queue:&wgpu::Queue,scene:&Scene) {
    let mut encoder=device.create_command_encoder(&wgpu::CommandEncoderDescriptor {
        label:Some("Noqeri Vulkan GPU frame")
    });
    {
        let mut pass=encoder.begin_render_pass(&wgpu::RenderPassDescriptor{
            label:Some("offscreen 2D/3D renderpass"),
            color_attachments:&[Some(wgpu::RenderPassColorAttachment{
                view:&scene._color_view,resolve_target:None,
                ops:wgpu::Operations{
                    load:wgpu::LoadOp::Clear(wgpu::Color{r:0.035,g:0.05,b:0.085,a:1.0}),
                    store:wgpu::StoreOp::Store
                }
            })],
            depth_stencil_attachment:scene._depth_view.as_ref().map(|view|
                wgpu::RenderPassDepthStencilAttachment{
                    view,
                    depth_ops:Some(wgpu::Operations{
                        load:wgpu::LoadOp::Clear(1.0), store:wgpu::StoreOp::Store
                    }),
                    stencil_ops:None
                }
            ),
            timestamp_writes:None,occlusion_query_set:None
        });
        if let Some(buffer)=&scene.vertex_buffer {
            pass.set_vertex_buffer(0,buffer.slice(..));
            for (i,pipeline) in scene.pipelines.iter().enumerate() {
                pass.set_pipeline(pipeline);
                let n=if scene.kind=="cube" {6} else {3};
                pass.draw((i as u32*n)..((i as u32+1)*n),0..1);
            }
        } else {
            pass.set_pipeline(&scene.pipelines[0]);
            pass.draw(0..3,0..1);
        }
    }
    queue.submit([encoder.finish()]);
    device.poll(wgpu::Maintain::Wait);
}

fn time_frames(device:&wgpu::Device,queue:&wgpu::Queue,scene:&Scene) -> Timings {
    let mut times=Vec::new();
    for i in 0..(WARMUPS+SAMPLES) {
        let start=Instant::now();
        frame(device,queue,scene);
        if i>=WARMUPS {times.push(start.elapsed().as_secs_f64()*1000.0);}
    }
    quantile_stats(times)
}

fn pixels_readback(device:&wgpu::Device,queue:&wgpu::Queue,scene:&Scene) -> Result<Vec<u8>> {
    let row_bytes=scene.width*4;
    let padded=((row_bytes+255)/256)*256;
    let size=padded as u64*scene.height as u64;
    let buffer=device.create_buffer(&wgpu::BufferDescriptor {
        label:Some("Noqeri GPU color pixel readback"),
        size, usage:wgpu::BufferUsages::MAP_READ|wgpu::BufferUsages::COPY_DST,
        mapped_at_creation:false
    });
    let mut encoder=device.create_command_encoder(&wgpu::CommandEncoderDescriptor{
        label:Some("GPU -> CPU image transfer")
    });
    encoder.copy_texture_to_buffer(
        wgpu::ImageCopyTexture{
            texture:&scene.texture,mip_level:0,origin:wgpu::Origin3d::ZERO,
            aspect:wgpu::TextureAspect::All
        },
        wgpu::ImageCopyBuffer{
            buffer:&buffer,
            layout:wgpu::ImageDataLayout{
                offset:0,bytes_per_row:Some(padded),rows_per_image:Some(scene.height)
            }
        },
        wgpu::Extent3d{width:scene.width,height:scene.height,depth_or_array_layers:1}
    );
    queue.submit([encoder.finish()]);
    let (sender,receiver)=mpsc::channel();
    buffer.slice(..).map_async(wgpu::MapMode::Read,move |result|{
        let _=sender.send(result);
    });
    device.poll(wgpu::Maintain::Wait);
    receiver.recv()??;
    let mut pixels=vec![0u8;(row_bytes*scene.height) as usize];
    {
        let mapped=buffer.slice(..).get_mapped_range();
        for y in 0..scene.height as usize {
            let from=y*padded as usize;
            let to=y*row_bytes as usize;
            pixels[to..to+row_bytes as usize].copy_from_slice(&mapped[from..from+row_bytes as usize]);
        }
    }
    buffer.unmap();
    Ok(pixels)
}

fn png_write(path:&Path,width:u32,height:u32,data:&[u8]) -> Result<()> {
    let file=fs::File::create(path)?;
    let mut encoder=png::Encoder::new(file,width,height);
    encoder.set_color(png::ColorType::Rgba);
    encoder.set_depth(png::BitDepth::Eight);
    let mut w=encoder.write_header()?;
    w.write_image_data(data)?;
    Ok(())
}

fn compare(nq:&[u8],ref_pixels:&[u8]) -> Result<Comparison> {
    if nq.len()!=ref_pixels.len() {return Err("pixel buffer lengths differ".into());}
    let mut max=0u8;
    let mut mismatch=0usize;
    let mut colored=0usize;
    let mut buckets=BTreeSet::new();
    let bg=[9u8,13u8,22u8];
    for (a,b) in nq.iter().zip(ref_pixels.iter()) {
        let d=a.abs_diff(*b);
        max=max.max(d);
        if d>2 {mismatch+=1;}
    }
    for pixel in nq.chunks_exact(4) {
        let color=[pixel[0],pixel[1],pixel[2]];
        if color.iter().zip(bg).any(|(a,b)|a.abs_diff(b)>5) {
            colored+=1;
        }
        buckets.insert((color[0]/24,color[1]/24,color[2]/24));
    }
    Ok(Comparison{
        max_abs_channel_difference:max,
        changed_channels_over_2:mismatch,
        sample_pixels:nq.len()/4,
        colored_pixels:colored,
        distinct_color_buckets:buckets.len()
    })
}

fn path_list(root:&Path,kind:&str,implementation:&str)->Vec<String> {
    let names=match kind{
        "gradient"=>vec!["color_gradient"],
        "posterize"=>vec!["posterize_fragment"],
        "vector"=>vec!["vector_fragment"],
        "depth"=>vec!["vertex3d","fragment3d","fragment_near"],
        "cube"=>vec!["vertex3d","face0","face1","face2","face3","face4","face5"],
        _=>vec![]
    };
    names.into_iter().map(|s|root.join(implementation).join(format!("{s}.wgsl")).to_string_lossy().to_string()).collect()
}

fn run()->Result<()> {
    let arg=env::args().nth(1).ok_or("usage: gpu-headless <shader-root>")?;
    let root=PathBuf::from(&arg);
    let out=root.join("images");
    fs::create_dir_all(&out)?;
    let instance=wgpu::Instance::new(wgpu::InstanceDescriptor{
        backends:wgpu::Backends::VULKAN,..Default::default()
    });
    let adapter=pollster::block_on(instance.request_adapter(&wgpu::RequestAdapterOptions{
        power_preference:wgpu::PowerPreference::LowPower,
        compatible_surface:None,
        force_fallback_adapter:true
    })).ok_or("No Vulkan software fallback adapter; cannot claim GPU rendering")?;
    let info=adapter.get_info();
    println!("GPU_ADAPTER name={} vendor={} device={} type={:?} backend={:?}",
        info.name,info.vendor,info.device,info.device_type,info.backend);
    let (device,queue)=pollster::block_on(adapter.request_device(&wgpu::DeviceDescriptor{
        label:Some("Noqeri shader offscreen Vulkan benchmark"),
        required_features:wgpu::Features::empty(),
        required_limits:wgpu::Limits::downlevel_defaults().using_resolution(adapter.limits())
    },None))?;
    let cases=[("gradient",256u32),("gradient",512),("posterize",256),("vector",256),("depth",256),("cube",512)];
    let mut records=Vec::new();
    for (kind,size) in cases {
        let mut n=scene_init(&device,&root,kind,size,size,"noqeri")?;
        let mut r=scene_init(&device,&root,kind,size,size,"reference")?;
        let tn=time_frames(&device,&queue,&n);
        let tr=time_frames(&device,&queue,&r);
        frame(&device,&queue,&n);
        frame(&device,&queue,&r);
        let nq=pixels_readback(&device,&queue,&n)?;
        let reference=pixels_readback(&device,&queue,&r)?;
        let comparison=compare(&nq,&reference)?;
        if comparison.changed_channels_over_2!=0 {
            return Err(format!("{kind} {size}: {} channels differ >2 (max {}), GPU result not equivalent",
                comparison.changed_channels_over_2,comparison.max_abs_channel_difference).into());
        }
        if comparison.colored_pixels<size as usize*size as usize/20 {
            return Err(format!("{kind} {size}: empty/mostly background render, reject").into());
        }
        if (kind=="cube"||kind=="depth") && comparison.distinct_color_buckets<3 {
            return Err(format!("{kind} {size}: 3D scene missing varied depth/colors").into());
        }
        let path_nq=out.join(format!("{kind}-{size}-noqeri.png"));
        let path_ref=out.join(format!("{kind}-{size}-reference.png"));
        png_write(&path_nq,size,size,&nq)?;
        png_write(&path_ref,size,size,&reference)?;
        let ratio=tn.median_ms/tr.median_ms;
        println!("GPU_SCENE_RESULT {}",serde_json::json!({
            "kind":kind,"size":size,
            "noqeri_ms":tn.median_ms,"reference_ms":tr.median_ms,
            "ratio":ratio,
            "pixels":comparison.sample_pixels,
            "color_channels_differ_over_2":comparison.changed_channels_over_2,
            "max_channel_delta":comparison.max_abs_channel_difference,
            "colored_pixels":comparison.colored_pixels
        }));
        records.push(SceneRecord{
            scene:kind.into(),width:size,height:size,source:"Noqeri -> WGSL -> Vulkan software GPU".into(),
            nq_wgsl:path_list(&root,kind,"noqeri"),
            reference_wgsl:path_list(&root,kind,"reference"),
            nq_png:path_nq.display().to_string(),
            reference_png:path_ref.display().to_string(),
            noqeri:tn,reference:tr,speed_ratio_noqeri_to_reference:ratio,
            comparison
        });
    }
    let report=serde_json::json!({
        "schema":1,
        "renderer":"wgpu-native Vulkan offscreen GPU; Noqeri Stage-0 emits WGSL",
        "adapter_name":info.name,
        "adapter_type":format!("{:?}",info.device_type),
        "adapter_backend":format!("{:?}",info.backend),
        "adapter_vendor":info.vendor,
        "adapter_device":info.device,
        "force_fallback_adapter":true,
        "physical_gpu_qualified":false,
        "wall_clock_timing":"CPU to GPU submit, wait for completion, excluding pipeline creation and image readback; not pure GPU timestamps",
        "warmups":WARMUPS,
        "samples":SAMPLES,
        "color_compare":"Noqeri WGSL image vs independently authored WGSL; max 2 per-channel RGBA8 difference; images must contain visible content",
        "scenes":records
    });
    fs::write(root.join("gpu-results.json"),serde_json::to_vec_pretty(&report)?)?;
    println!("GPU_RENDER_PASS {}",root.join("gpu-results.json").display());
    Ok(())
}
fn main() {
    if let Err(e)=run(){
        eprintln!("GPU_BENCHMARK_FAILED: {e:?}");
        std::process::exit(1)
    }
}
