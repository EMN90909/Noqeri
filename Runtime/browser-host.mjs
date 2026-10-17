// Explicit browser capability adapter for Noqeri-generated ES modules.
// Supply only functions you intend Noqeri source to call.
export function installNoqeriBrowserHost({functions = {}} = {}) {
  if (typeof document === 'undefined') throw Error('Browser DOM is unavailable')
  const builtins = {
    domText(selector, value) {
      const element=document.querySelector(selector)
      if(!element)return false
      element.textContent=String(value)
      return true
    },
    domAddClass(selector, className) {
      const element=document.querySelector(selector)
      if(!element)return false
      element.classList.add(String(className))
      return true
    },
    domRemoveClass(selector, className) {
      const element=document.querySelector(selector)
      if(!element)return false
      element.classList.remove(String(className))
      return true
    }
  }
  for(const [name,fn] of Object.entries(functions)) {
    if(!/^[a-zA-Z_$][\w$]*$/.test(name)||['__proto__','constructor','prototype'].includes(name)||typeof fn!=='function') throw Error('Invalid host capability '+name)
    if(Object.hasOwn(builtins,name))throw Error('Cannot override built-in host capability '+name)
    Object.defineProperty(builtins,name,{value:fn,enumerable:true})
  }
  const old=globalThis.NoqeriHost
  globalThis.NoqeriHost=Object.freeze(Object.assign(Object.create(null),builtins))
  return () => {if(old===undefined)delete globalThis.NoqeriHost;else globalThis.NoqeriHost=old}
}
