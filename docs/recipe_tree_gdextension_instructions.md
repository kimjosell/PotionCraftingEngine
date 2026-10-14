# Instrucciones: exponer el árbol de recetas como objetos Godot (GDExtension)

Instrucciones para un agente que va a extender el Effect Engine + su GDExtension.
Objetivo: que Godot deje de recibir solo *handles opacos* y pueda **recorrer el árbol
binario de una receta** e inspeccionar las dimensiones de cada ingrediente como
**instancias de objetos con clases** (no punteros).

> **Estado (importante):** el **Paso 1 (extender la C API) YA ESTÁ HECHO** en este repo —
> ver `effect_engine_c.h` / `effect_engine_c.cpp`, verificado por `c_api_smoke.cpp`. El §4
> de abajo queda como **referencia de la API que ya existe**; NO la reimplementes. Tu
> trabajo real es la **capa GDExtension (§5 en adelante)**.

Lee primero, sin saltarte nada, estos archivos del repo — son la fuente de verdad:

- `include/effect_engine/engine.h` — define el árbol: `Ingredient`, `SourceObject`,
  `MysteriousObject`, `Recipe`, `MixRecipe`, `AlterRecipe`, `CraftedObject`.
- `include/effect_engine/effect_engine_c.h` + `src/effect_engine_c.cpp` — la C API actual
  (handles opacos `EEIngredient`/`EECrafted`, accesores planos). **Aquí falta el árbol.**
- `project_context/setup_godot.md` §3–§4 — el wrapper GDExtension actual y cómo se registra.
  Vas a **extender** ese wrapper, no reemplazarlo.

---

## 1. Diagnóstico: qué falta hoy

El árbol ya vive en C++ y sigue vivo mientras exista el handle (ver el comentario de
`effect_engine_c.h` §10/§15: los objetos cruzan como handles opacos propiedad del caller).
Su forma real:

```
CraftedObject
  └─ recipe            : shared_ptr<Recipe>   (MixRecipe | AlterRecipe)
MysteriousObject (es un Ingredient)
  └─ recipe            : shared_ptr<Recipe>
MixRecipe   : Recipe { operationId; int time; ingredientA; ingredientB; }   ← nodo BINARIO
AlterRecipe : Recipe { operationId; string alterObjectId; ingredient; }      ← nodo UNARIO
Ingredient (hoja o rama):
  SourceObject     : { id; tier; dimensions }                 ← HOJA
  MysteriousObject : { id; dimensions; recipe }               ← RAMA (recursión)
```

El árbol **alterna** Ingredient ↔ Recipe: una receta tiene 1–2 ingredientes hijos, y cada
ingrediente es o una hoja (`SourceObject`) o una rama con su propia receta
(`MysteriousObject`). `dimensions` es `vector<DimensionTriple>`, un triple `array<int,3>`
por dominio (`kDimensionsPerDomain == 3`).

**El problema:** `effect_engine_c.h` NO expone nada del árbol. No hay forma de preguntar
"¿de qué operación salió este objeto? ¿cuáles fueron sus dos ingredientes?". Solo se puede
leer el objeto final (dimensiones, effect id, dominios activos). No puedes envolver en
Godot lo que el ABI no expone — por eso el trabajo empieza en la C API.

---

## 2. Decisión de diseño (léela antes de escribir código)

Hay dos maneras válidas de llevar el árbol a Godot. Elige **una** según lo que el juego
necesite de verdad:

| | **A. Volcado a `Dictionary`** (recomendado para *visualizar*) | **B. Clases-nodo envueltas** (si hace falta *manipular*) |
|---|---|---|
| Qué cruza | Una llamada recursiva serializa el árbol entero a `Dictionary`/`Array` anidados | Cada nodo es un `RefCounted` con getters tipados; se recorre bajo demanda |
| C API nueva | 1 tipo opaco no-propietario + accesores de nodo | igual (mismos accesores) |
| Código GDExtension | ~1 función que recursa y arma el `Dictionary` | familia de clases-nodo + gestión de lifetime |
| Para qué sirve | llenar un `Tree`, imprimir, debug — inmediato | pasar nodos como objetos, lazy-load, inspector |
| Coste | ~1/4 del código de B | más código y un footgun de lifetime (ver §5) |

**Recomendación:** el usuario pidió "ver los árboles binarios fácilmente". Para eso, **A es
suficiente y mucho menos código** — un `Dictionary` anidado alimenta un `Tree` de Godot
directamente. Implementa **A** salvo que el juego realmente vaya a *pasar nodos como
objetos y mutarlos*; en ese caso implementa **B**. La C API del §4 es **la misma para las
dos** — la diferencia está solo en la capa GDExtension (§5). No implementes ambas.

> Nota: el árbol de recetas es **historia inmutable** (cómo se fabricó el objeto). No se
> "manipula" una receta; se lee. Los ingredientes sí se manipulan (mix/alter) y eso YA lo
> cubre el wrapper actual. Esto es otro argumento para A.

---

## 3. Reglas de lifetime y ABI (no las rompas)

1. **Los nodos del árbol son vistas NO-propietarias.** Viven dentro de un `EEIngredient` o
   `EECrafted` ya existente y son válidos **solo mientras ese handle raíz siga vivo**. No se
   asignan ni se liberan por separado. → Usa un **tipo opaco nuevo y distinto**
   (`EERecipeNode`) para que nadie llame por error a `effect_engine_ingredient_free` sobre
   una vista. NO reutilices `EEIngredient` para las vistas del subárbol.
2. **Nada de excepciones cruzando `extern "C"`.** Cada función nueva: `try/catch`, y en
   fallo → `setError(...)` + retorno nulo/`-1`, igual que el resto de `effect_engine_c.cpp`.
   Limpia el error al entrar (`clearError()`).
3. **Los `const char*` devueltos apuntan a memoria del engine** y valen hasta
   `effect_engine_destroy` (o hasta que se libere el handle raíz). No los liberes. Cópialos
   en Godot con `String(...)`.
4. **Dimensiones = arrays planos de int**, mismo patrón que hoy: `count()` + `dims(out,cap)`
   escribiendo `out[domain*3 + dim]`, retornando `-1` si `out` es null o `cap` insuficiente.
   Reusa el helper `flattenDimensions` y `copyInts` que ya están en el `.cpp`.

---

## 4. Paso 1 — C API (YA IMPLEMENTADA — referencia)

> Esta sección documenta la API que **ya existe** en `effect_engine_c.h`. Es referencia para
> escribir el wrapper del §5; no hay que reimplementarla. Los nombres/firmas de abajo son
> exactamente los que están en el repo.

Modela **un solo tipo de nodo** que representa un ingrediente dentro del árbol, porque es lo
que el usuario ya entiende. En `include/effect_engine/effect_engine_c.h`:

```c
/* Vista NO-propietaria de un nodo del árbol de recetas. Válida mientras viva el
   EEIngredient / EECrafted del que se obtuvo. NUNCA se libera. */
typedef struct EERecipeNode EERecipeNode;

/* Tipo de nodo. */
typedef enum {
    EE_NODE_SOURCE     = 0,  /* hoja: SourceObject (tiene id + tier + dimensiones)      */
    EE_NODE_MIX        = 1,  /* rama binaria: MixRecipe (2 hijos, operation_id, time)   */
    EE_NODE_ALTER      = 2   /* rama unaria: AlterRecipe (1 hijo, operation_id, alter)  */
} EENodeKind;

/* Raíces del árbol (vistas no-propietarias): */
EE_API const EERecipeNode* effect_engine_ingredient_root(const EEIngredient* ingredient);
EE_API const EERecipeNode* effect_engine_crafted_root(const EECrafted* crafted);

/* Interrogar un nodo: */
EE_API int         effect_engine_node_kind(const EERecipeNode* node); /* EENodeKind, -1 err */
EE_API const char* effect_engine_node_id(const EERecipeNode* node);   /* ingredient id, "" si n/a */
EE_API const char* effect_engine_node_tier(const EERecipeNode* node); /* solo SOURCE, "" si no */
EE_API const char* effect_engine_node_operation_id(const EERecipeNode* node); /* MIX/ALTER, "" si no */
EE_API int         effect_engine_node_time(const EERecipeNode* node); /* solo MIX, -1 si no */
EE_API const char* effect_engine_node_alter_object_id(const EERecipeNode* node); /* solo ALTER, "" si no */

/* Dimensiones del ingrediente en este nodo (mismo patrón plano que ingredient_dimensions). */
EE_API int effect_engine_node_domain_count(const EERecipeNode* node);
EE_API int effect_engine_node_dimensions(const EERecipeNode* node, int* out, int cap);

/* Hijos (vistas no-propietarias). SOURCE: 0 hijos. ALTER: 1. MIX: 2.
   Devuelve NULL si el índice no existe para ese tipo de nodo. */
EE_API int                 effect_engine_node_child_count(const EERecipeNode* node);
EE_API const EERecipeNode* effect_engine_node_child(const EERecipeNode* node, int index);
```

### Cómo está implementada en `src/effect_engine_c.cpp` (referencia)

`EERecipeNode` es una **fachada no-propietaria sobre un `const Ingredient*`** del árbol vivo
(el árbol ya es dueño de los `shared_ptr`; solo pasamos punteros crudos que apuntan dentro).
`EERecipeNode` se queda como tipo incompleto en el ABI y se hace `reinterpret_cast` entre la
vista opaca y el `Ingredient` real (misma dirección, sin asignar, sin ownership):

```cpp
const EERecipeNode* asNode(const Ingredient* ing) { return reinterpret_cast<const EERecipeNode*>(ing); }
const Ingredient* asIngredient(const EERecipeNode* n) { return reinterpret_cast<const Ingredient*>(n); }
```

- `effect_engine_ingredient_root(ing)` → `asNode(ing->obj.get())`.
- `effect_engine_crafted_root(crafted)` → **resuelto:** `craft()` (ver `engine.cpp:132`) solo
  conserva `crafted.recipe` (un `Recipe`, no el ingrediente). Por eso `EECrafted` **retiene
  el `shared_ptr<Ingredient>` del mysterious de entrada** (`handle->rootIngredient =
  mysterious->obj;` en `effect_engine_craft`). Sus dimensiones son iguales a las coords
  finales del efecto (craft no las cambia, `engine.cpp:119`) y su `recipe` es el árbol
  entero. `crafted_root` devuelve `asNode(crafted->rootIngredient.get())`. Es un `shared_ptr`
  refcontado, así que el árbol sigue vivo aunque el juego libere su `EEIngredient` de entrada.

Para interrogar un nodo se hace `dynamic_cast` sobre el `Ingredient`:

```cpp
int effect_engine_node_kind(const EERecipeNode* node) {
    clearError();
    if (!node) { setError("null node"); return -1; }
    const Ingredient* ing = asIngredient(node);
    if (dynamic_cast<const SourceObject*>(ing)) return EE_NODE_SOURCE;
    const auto* myst = dynamic_cast<const MysteriousObject*>(ing);
    if (!myst || !myst->recipe) { setError("node has no recipe"); return -1; }
    if (dynamic_cast<const MixRecipe*>(myst->recipe.get()))   return EE_NODE_MIX;
    if (dynamic_cast<const AlterRecipe*>(myst->recipe.get())) return EE_NODE_ALTER;
    setError("unknown recipe kind"); return -1;
}
```

- `node_id` → `ing->id.c_str()`.
- `node_tier` → si es `SourceObject`, su `tier`; si no, `""`.
- `node_operation_id` → el `recipe->operationId` del `MysteriousObject`; `""` para hoja.
- `node_time` → `MixRecipe::time`; `-1` si no es MIX.
- `node_alter_object_id` → `AlterRecipe::alterObjectId`; `""` si no es ALTER.
- `node_domain_count` / `node_dimensions` → sobre `ing->dimensions`, reusando
  `flattenDimensions` + `copyInts` (idénticos a `ingredient_dimensions`).
- `node_child_count`:
  - SOURCE → 0
  - ALTER → 1
  - MIX → 2
- `node_child(node, index)`:
  - MIX: index 0 → `asNode(mix->ingredientA.get())`, index 1 → `asNode(mix->ingredientB.get())`.
  - ALTER: index 0 → `asNode(alter->ingredient.get())`.
  - fuera de rango → `setError(...)`, `NULL`.

> `ingredientA`/`ingredientB`/`ingredient` son `shared_ptr<Ingredient>` propiedad del árbol
> vivo; devolver `.get()` como vista es seguro **mientras el handle raíz viva** (§3.1).

**Verificación (ya presente):** `src/c_api_smoke.cpp` recorre el árbol de
`brew = mix(chamomile, mix(chamomile, chamomile))`, verificando `EE_NODE_MIX` en la raíz,
`child_count == 2`, la hoja `EE_NODE_SOURCE` "chamomile"/"standard", la recursión en el hijo
derecho y las dimensiones de la hoja. Reconstruye y córrelo con
`cmake --build build && ./build/c_api_smoke` (desde la raíz del repo — necesita el config).

---

## 5. Paso 2 — Capa GDExtension

Extiende `src/ee_wrapper.h` del proyecto godot-cpp (el de `setup_godot.md` §3). Elige A o B.

### Opción A — `get_recipe_tree()` → `Dictionary` (recomendada)

Añade **un solo método** a `EEIngredientRef` (y otro igual a `EECraftedRef`) que recursa la
C API y devuelve un `Dictionary` anidado. Sin clases nuevas, sin lifetime que gestionar
(el árbol se copia a valores Godot en una pasada, mientras el handle está vivo).

```cpp
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/array.hpp>

static Dictionary node_to_dict(const EERecipeNode *n) {
    Dictionary d;
    int kind = effect_engine_node_kind(n);
    d["kind"] = kind;                                   // 0 source / 1 mix / 2 alter
    d["id"]   = String(effect_engine_node_id(n));
    // dimensiones planas
    PackedInt32Array dims;
    int m = effect_engine_node_domain_count(n) * 3;
    dims.resize(m);
    effect_engine_node_dimensions(n, dims.ptrw(), m);
    d["dimensions"] = dims;
    if (kind == 0) {                                    // SOURCE
        d["tier"] = String(effect_engine_node_tier(n));
    } else {                                            // MIX / ALTER
        d["operation_id"] = String(effect_engine_node_operation_id(n));
        if (kind == 1) d["time"] = effect_engine_node_time(n);
        if (kind == 2) d["alter_object_id"] = String(effect_engine_node_alter_object_id(n));
        Array children;
        int c = effect_engine_node_child_count(n);
        for (int i = 0; i < c; ++i)
            children.push_back(node_to_dict(effect_engine_node_child(n, i)));
        d["children"] = children;
    }
    return d;
}
// dentro de EEIngredientRef:  Dictionary get_recipe_tree() const { return node_to_dict(effect_engine_ingredient_root(handle)); }
// dentro de EECraftedRef:     Dictionary get_recipe_tree() const { return node_to_dict(effect_engine_crafted_root(handle)); }
```

Bindea `get_recipe_tree` en el `_bind_methods` de cada clase. Nada más que registrar (las
clases ya están en `register_types.cpp`).

### Opción B — clases-nodo `EERecipeNodeRef` (solo si hace falta manipular)

`EERecipeNodeRef : RefCounted` envuelve un `const EERecipeNode*` **más un `Ref` al wrapper
raíz** para mantener vivo el árbol C++:

```cpp
class EERecipeNodeRef : public RefCounted {
    GDCLASS(EERecipeNodeRef, RefCounted)
public:
    const EERecipeNode *node = nullptr;
    Ref<RefCounted> root_keepalive;   // ← CRÍTICO: mantiene vivo el EEIngredientRef/EECraftedRef
                                      //   dueño del árbol. Sin esto, el árbol se libera y
                                      //   `node` queda colgando (use-after-free).
    int get_kind() const { return effect_engine_node_kind(node); }
    String get_id() const { return String(effect_engine_node_id(node)); }
    // ... get_operation_id, get_time, get_alter_object_id, get_dimensions (patrón del §5A) ...
    Array get_children() const {
        Array out;
        int c = effect_engine_node_child_count(node);
        for (int i = 0; i < c; ++i) {
            Ref<EERecipeNodeRef> child; child.instantiate();
            child->node = effect_engine_node_child(node, i);
            child->root_keepalive = root_keepalive;   // propaga el keepalive a cada hijo
            out.push_back(child);
        }
        return out;
    }
protected:
    static void _bind_methods() { /* bindea todos los getters */ }
};
```

En `EEIngredientRef::get_root()` / `EECraftedRef::get_root()`: instancia un
`EERecipeNodeRef`, setea `node = effect_engine_*_root(handle)` y
`root_keepalive = Ref<RefCounted>(this)`. **Registra `EERecipeNodeRef`** con
`GDREGISTER_RUNTIME_CLASS(EERecipeNodeRef)` en `register_types.cpp` (junto a las tres que ya
están — ver `setup_godot.md` §4).

> **El footgun de B** es `root_keepalive`. Los `EERecipeNode` son punteros dentro del árbol
> del handle raíz; si el `EEIngredientRef` se libera, el árbol muere y todos los nodos
> quedan colgando. El `Ref` al raíz lo evita. Esto es todo el coste extra de B sobre A.

### AlterObject — exponerlo igual (SIN tocar la C API)

Un `AlterObject` (`engine.h`) es plano: solo `id` + `exponents` (un exponente entero por
dominio, `[1, modulus-2]`). No es un `Ingredient`, no se mezcla ni se craftea, y **no tiene
handle propio** — es dato de config referenciado por id. Por eso, a diferencia del árbol de
recetas, **no hay nada que abrir en la C API: ya está todo expuesto**. Solo hay que
envolverlo. Funciones que YA existen y vas a usar:

- `effect_engine_alter_exponents(engine, id, out, cap)` → escribe los exponentes, devuelve
  cuántos, o `-1` si el id no existe / buffer chico. Hay **un exponente por dominio**, así
  que dimensiona el buffer con `effect_engine_domain_count(engine)`.
- `effect_engine_alter_object_count(engine)` / `effect_engine_alter_object_id(engine, i)` →
  listar los alter objects disponibles (para menús de UI).

Como es dato plano de config (sin puntero opaco, sin lifetime que cuidar), un `Dictionary`
es la representación natural — aquí **no vale la pena** una clase-nodo. Añade a
`EffectEngineGD` (la clase engine de `setup_godot.md` §3):

```cpp
#include <godot_cpp/variant/packed_int32_array.hpp>

// { "id": String, "exponents": PackedInt32Array } — o un Dictionary vacío si el id no existe.
Dictionary get_alter_object(const String &id) const {
    int n = effect_engine_domain_count(engine);            // un exponente por dominio
    PackedInt32Array ex; ex.resize(n);
    int written = effect_engine_alter_exponents(engine, id.utf8().get_data(), ex.ptrw(), n);
    if (written < 0) return Dictionary();                  // id desconocido → last_error()
    Dictionary d; d["id"] = id; d["exponents"] = ex;
    return d;
}
// (opcional) listar todos, para poblar un menú:
PackedStringArray list_alter_objects() const {
    PackedStringArray out;
    int c = effect_engine_alter_object_count(engine);
    for (int i = 0; i < c; ++i) out.push_back(String(effect_engine_alter_object_id(engine, i)));
    return out;
}
```

Bindea ambos en `_bind_methods` de `EffectEngineGD`. No hay clase nueva que registrar.

**Conexión con el árbol:** un nodo `EE_NODE_ALTER` te da `alter_object_id` (via
`node_alter_object_id` / la clave `"alter_object_id"` del Dictionary del §5A). Con ese id
llamas a `get_alter_object(id)` y obtienes sus exponentes — así, al recorrer el árbol,
puedes mostrar también *con qué* se alteró cada rama.

Con la opción A, visualizar el árbol binario en un control `Tree`:

```gdscript
func _fill(tree_item: TreeItem, node: Dictionary) -> void:
    var kind := int(node["kind"])
    var label := node["id"] as String
    match kind:
        0: label = "🍃 %s [%s]" % [node["id"], node["tier"]]
        1: label = "⚗️ mix:%s (t=%d)" % [node["operation_id"], int(node["time"])]
        2: label = "✨ alter:%s" % node["alter_object_id"]
    tree_item.set_text(0, label)
    tree_item.set_text(1, str(node["dimensions"]))   # dimensiones del ingrediente
    for child in node.get("children", []):
        _fill(tree.create_item(tree_item), child)

func show_recipe(brew) -> void:   # brew = un EEIngredientRef de mix/alter
    tree.clear()
    _fill(tree.create_item(), brew.get_recipe_tree())
```

Esto imprime el árbol binario completo (raíz = la operación, hojas = los sources) con las
dimensiones de cada ingrediente en la segunda columna. Exactamente lo que pediste.

---

## 7. Criterios de aceptación (verifícalos)

1. `cmake --build build` compila la librería sin warnings nuevos; el smoke test del §4 pasa.
2. En Godot, tras un `mix(a, b, ...)`, `brew.get_recipe_tree()` devuelve un `Dictionary` con
   `kind == 1`, `children.size() == 2`, y cada hijo `kind == 0` con el `id`/`tier` correctos.
3. Un `alter(brew, ...)` produce `kind == 2` con `children.size() == 1` y `alter_object_id`
   correcto; su hijo es el subárbol del `mix` anterior (recursión verificada).
4. `crafted.get_recipe_tree()` sobre un resultado de `craft()` devuelve el mismo árbol.
5. Ningún `_free` se llama sobre una vista de nodo; ningún crash al soltar el `EEIngredientRef`
   mientras se sigue leyendo (solo relevante en opción B → prueba que `root_keepalive` lo
   evita).
6. `engine.get_alter_object(id)` con un id válido de tu config devuelve `{ id, exponents }`
   con un exponente por dominio; con un id inexistente devuelve un `Dictionary` vacío y
   `engine.last_error()` explica por qué.

## 8. Errores a evitar

- **Reutilizar `EEIngredient` para las vistas de subárbol.** Provoca doble free. Usa el tipo
  distinto `EERecipeNode` (§3.1).
- **Dejar que una excepción cruce `extern "C"`.** Envuelve todo en `try/catch` (§3.2).
- **(Opción B) Olvidar `root_keepalive`.** Use-after-free en cuanto el GC de Godot suelte la
  raíz (§5B).
- **Reimplementar la C API.** Ya existe y está testeada (§4); solo escribe el wrapper Godot.
- **Implementar A *y* B.** Elige una (§2). Para "ver el árbol fácilmente", A basta.
```
