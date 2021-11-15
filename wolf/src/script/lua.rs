use std::str::from_utf8;

#[cxx::bridge]
pub mod ffi {

    #[repr(u8)]
    #[derive(Debug, Clone, Copy)]
    enum LuaType {
        L_NIL = 0,
        L_BOOLEAN,
        L_LIGHTUSERDATA, //Not implemented yet
        L_NUMBER,
        L_STRING,
        L_TABLE,
        L_FUNCTION,
        L_USERDATA, //Not implemented yet
        L_THREAD,   //Not implemented yet
    }

    #[derive(Debug)]
    pub struct LuaValue {
        t: LuaType, //type
        v: Vec<u8>, //value in bytes
    }

    unsafe extern "C++" {
        include!("wolf/src/script/cxx/luaJIT/luaJIT.hpp");

        //pub type c_void;
        pub type lua_State;
        type lua_CFunction = super::lua_CFunction;

        pub type luaJIT;
        pub fn load(self: &luaJIT, p_module_name: &str, p_source_code: &str) -> i32;
        pub fn exec(self: &luaJIT) -> i32;
        pub fn bind(self: &luaJIT, p_lua_fn_name: &str, p_lua_c_fun: lua_CFunction) -> i32;
        pub fn call(
            self: &luaJIT,
            p_name: &str,
            p_params: &[LuaValue],
            p_results: &mut Vec<LuaValue>,
        ) -> i32;
        pub fn get_global(self: &luaJIT, p_name: &str, p_value: &mut LuaValue) -> i32;
        pub fn set_global(self: &luaJIT, p_name: &str, p_value: &LuaValue) -> i32;
        pub fn set_lua_path(self: &luaJIT, p_env_path: &str) -> i32;

        pub fn New() -> UniquePtr<luaJIT>;
    }
}

impl ffi::LuaValue {
    pub fn new() -> Self {
        Self {
            t: ffi::LuaType::L_NIL,
            v: Vec::<u8>::new(),
        }
    }

    #[inline]
    pub fn get_type(&self) -> ffi::LuaType {
        self.t
    }

    #[inline]
    pub fn from_bool(p: bool) -> Self {
        let value = ffi::LuaValue {
            t: ffi::LuaType::L_BOOLEAN,
            v: vec![p as u8; 1],
        };
        value
    }

    #[inline]
    pub fn as_bool(&self) -> anyhow::Result<bool> {
        const SIZE: usize = std::mem::size_of::<bool>();
        if self.t == ffi::LuaType::L_BOOLEAN && self.v.len() == SIZE {
            Ok(self.v[0] != 0)
        } else {
            anyhow::bail!("could not cast lua::ffi::LuaValue to bool")
        }
    }

    #[inline]
    pub fn from_number(p: f64) -> Self {
        const SIZE: usize = std::mem::size_of::<f64>();
        let mut value = ffi::LuaValue {
            t: ffi::LuaType::L_NUMBER,
            v: vec![0u8; SIZE],
        };
        let bytes = p.to_ne_bytes();
        value.v.copy_from_slice(&bytes);
        value
    }

    #[inline]
    pub fn as_number(&self) -> anyhow::Result<f64> {
        const SIZE: usize = std::mem::size_of::<f64>();
        if self.t == ffi::LuaType::L_NUMBER && self.v.len() == SIZE {
            let v_res: anyhow::Result<&[u8; SIZE]> = self.v.as_slice().try_into().map_err(|e| {
                anyhow::anyhow!("could not cast lua::ffi::LuaValue to f64. error: {}", e)
            });
            let v = v_res?;
            let d = f64::from_ne_bytes(*v);
            Ok(d)
        } else {
            anyhow::bail!("could not cast lua::ffi::LuaValue to f64")
        }
    }

    #[inline]
    pub fn from_string(p: &str) -> Self {
        let size: usize = p.len();
        let mut value = ffi::LuaValue {
            t: ffi::LuaType::L_STRING,
            v: vec![0u8; size],
        };
        let bytes = p.as_bytes();
        value.v.copy_from_slice(&bytes);
        value
    }

    #[inline]
    pub fn as_string(&self) -> anyhow::Result<String> {
        if self.t == ffi::LuaType::L_STRING {
            let d = from_utf8(self.v.as_slice())?;
            Ok(d.to_string())
        } else {
            anyhow::bail!("could not cast lua::ffi::LuaValue to String")
        }
    }
}

#[repr(transparent)]
#[allow(non_camel_case_types)]
pub struct lua_CFunction(pub extern "C" fn(p_arg: ffi::lua_State) -> i32);
unsafe impl cxx::ExternType for lua_CFunction {
    type Id = cxx::type_id!("lua_CFunction");
    type Kind = cxx::kind::Trivial;
}

pub struct Lua {
    l: cxx::UniquePtr<ffi::luaJIT>,
    module_name: String,
}

impl Lua {
    pub fn new() -> Self {
        Self {
            l: ffi::New(),
            module_name: String::new(),
        }
    }

    pub async fn load(
        &mut self,
        p_module_name: &String,
        p_source_code: &String,
    ) -> anyhow::Result<()> {
        let res = self.l.load(p_module_name, p_source_code);
        let ret = match res {
            0 => {
                self.module_name = p_module_name.clone();
                Ok(())
            }
            _ => anyhow::bail!(
                "could not load lua module: {} with the following source code: {}",
                p_module_name,
                p_source_code
            ),
        };
        ret
    }

    pub async fn exec(&self) -> anyhow::Result<()> {
        let res = self.l.exec();
        let ret = match res {
            0 => Ok(()),
            _ => anyhow::bail!("could not execute lua module: {}", self.module_name,),
        };
        ret
    }

    pub async fn bind(
        &self,
        p_lua_func_name: &str,
        p_lua_callback_func: ffi::lua_CFunction,
    ) -> anyhow::Result<()> {
        let res = self.l.bind(p_lua_func_name, p_lua_callback_func);
        let ret = match res {
            0 => Ok(()),
            _ => anyhow::bail!(
                "could not bind lua function for lua module: {}",
                self.module_name,
            ),
        };
        ret
    }

    pub async fn call<'a>(
        &self,
        p_name: &str,
        p_params: Option<&'a [ffi::LuaValue]>,
    ) -> anyhow::Result<Vec<ffi::LuaValue>> {
        let params = p_params.unwrap_or(&[]);
        let mut outs = Vec::new();

        let res = self.l.call(p_name, params, &mut outs);
        let ret = match res {
            0 => Ok(outs),
            _ => anyhow::bail!(
                "could not execute function {} from lua module: {}",
                p_name,
                self.module_name,
            ),
        };
        ret
    }

    pub async fn get_global(&self, p_name: &str) -> anyhow::Result<ffi::LuaValue> {
        let mut value = ffi::LuaValue::new();
        let res = self.l.get_global(&p_name, &mut value);
        let ret = match res {
            0 => anyhow::Result::Ok(value),
            _ => anyhow::Result::Err(anyhow::anyhow!(
                "could not get global variable {} from lua module {}",
                p_name,
                self.module_name
            )),
        };
        ret
    }

    pub async fn set_global(&self, p_name: &str, p_value: &ffi::LuaValue) -> anyhow::Result<()> {
        let res = self.l.set_global(&p_name, &p_value);
        let ret = match res {
            0 => Ok(()),
            _ => Err(anyhow::anyhow!(
                "could not set global variable {} for lua module {}",
                p_name,
                self.module_name
            )),
        };
        ret
    }

    pub async fn set_local_path(&self, p_env_path: &str) -> anyhow::Result<()> {
        let res = self.l.set_lua_path(&p_env_path);
        let ret = match res {
            0 => Ok(()),
            _ => Err(anyhow::anyhow!(
                "could not set local path {} for lua module {}",
                p_env_path,
                self.module_name
            )),
        };
        ret
    }
}

#[tokio::test]
async fn test() -> core::result::Result<(), Box<dyn std::error::Error>> {
    let mut lua = Lua::new();
    lua.load(
        &"wolf_lua".to_string(),
        &r#"
    b = true
    i = 7
    d = 17.7
    s = "hello from lua"

    function f0 ()
        print("hello from function f0")
    end

    function f1 ()
        return 1.1
    end

    function f2 (arg1, arg2)
        if (arg1 > arg2) then
            return arg1, "arg1 is greater"
        else
            return arg2, "arg2 is greater"
        end
    end
    "#
        .to_string(),
    )
    .await?;

    lua.exec().await?;

    let mut b = lua.get_global(&"b").await?;
    let mut i = lua.get_global(&"i").await?;
    let mut d = lua.get_global(&"d").await?;
    let mut s = lua.get_global(&"s").await?;

    println!("lua b has {:?} value", b.as_bool());
    println!("lua i has {:?} value", i.as_number());
    println!("lua d has {:?} value", d.as_number());
    println!("lua s has {:?} value", s.as_string());

    let b_new = ffi::LuaValue::from_bool(false);
    let i_new = ffi::LuaValue::from_number(70.0);
    let d_new = ffi::LuaValue::from_number(70.7);
    let s_new = ffi::LuaValue::from_string("hello from rust");

    lua.set_global(&"b", &b_new).await?;
    lua.set_global(&"i", &i_new).await?;
    lua.set_global(&"d", &d_new).await?;
    lua.set_global(&"s", &s_new).await?;

    b = lua.get_global(&"b").await?;
    i = lua.get_global(&"i").await?;
    d = lua.get_global(&"d").await?;
    s = lua.get_global(&"s").await?;

    println!("lua b has {:?} value", b.as_bool());
    println!("lua i has {:?} value", i.as_number());
    println!("lua d has {:?} value", d.as_number());
    println!("lua s has {:?} value", s.as_string());

    //call void function
    let _f0_void = lua.call("f0", None).await?;

    let _f1_ret = lua.call("f1", None).await?;
    println!("lua function f1 returns:");
    for iter in _f1_ret.iter() {
        println!("{:?}", iter.as_number());
    }

    let _f2_ret = lua.call("f2", Some(&[d_new, i_new])).await?;
    println!("lua function f2 returns:");
    for iter in _f2_ret.iter() {
        let t = iter.get_type();
        match t {
            ffi::LuaType::L_NUMBER => {
                println!("{:?}", iter.as_number());
            }
            ffi::LuaType::L_STRING => {
                println!("{:?}", iter.as_string());
            }
            _ => {}
        }
    }

    Ok(())
}
