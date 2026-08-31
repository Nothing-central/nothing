# PART 4: DOM & STORAGE MONITORING

## The Ultimate Qt6 WebEngine Scraping Browser Guide

*Exhaustive implementation reference — every node, every mutation, every localStorage key to your scraper.*

---

## 4.1 The DOM Domain Architecture

### 4.1.1 The Node ID Map — The Heart of the Inspector

`InspectorDOMAgent` (`third_party/blink/renderer/core/inspector/inspector_dom_agent.cc`, 3,496 lines) is the renderer-side agent that owns the bidirectional mapping between live DOM `Node*` pointers (Blink C++ objects) and integer `nodeId`s (what the CDP client sees). Without understanding this mapping, you can't reason about DOM mutations, shadow DOM traversal, or iframe recursion.

```cpp
// inspector_dom_agent.h:377-440
class InspectorDOMAgent final : public InspectorBaseAgent<protocol::DOM::Metainfo> {
  // ...
  using NodeToIdMap = GCedHeapHashMap<Member<Node>, int>;
  
  // Bidirectional maps
  Member<NodeToIdMap> document_node_to_id_map_;              // For the root document
  HeapVector<Member<NodeToIdMap>> dangling_node_to_id_maps_;  // For detached subtrees
  HeapHashMap<int, Member<Node>> id_to_node_;                 // nodeId → Node*
  HeapHashMap<int, Member<NodeToIdMap>> id_to_nodes_map_;     // nodeId → its children's NodeToIdMap
  
  HashSet<int> children_requested_;             // nodeIds whose children we already pushed
  HashMap<int, int> cached_child_count_;        // lazy child counts (for collapsed nodes)
  
  int last_node_id_ = 0;                        // monotonic counter
};
```

The bidirectional invariant:
- `Bind(Node*, NodeToIdMap*)` (`:581-622`) assigns `++last_node_id_` to a `Node`, inserts into both `id_to_node_` and the per-document `NodeToIdMap`
- `Unbind(Node*)` recursively removes a Node and all its descendants (including shadow roots, iframes, and pseudo-elements)
- `NodeForId(int)` is O(1) hash lookup
- `BoundNodeId(Node*)` is O(1) hash lookup on the right `NodeToIdMap`

When a `Node` is GC'd by Blink (because it was removed from the DOM and no JS holds a reference), the `Member<Node>` weak handle in `id_to_node_` clears. But the `int → Member<Node>` entry stays, returning a null `Node*`. The CDP client gets `No node with given id found` errors if it tries to access that nodeId again.

### 4.1.2 The InspectorHistory + DOMEditor (Undo/Redo)

Every mutation command (`DOM.setAttributeValue`, `DOM.removeNode`, `DOM.setOuterHTML`, `CSS.setStyleTexts`, etc.) goes through the `InspectorHistory` + `DOMEditor` pair so undo/redo works.

```cpp
// inspector_dom_agent.cc:731-762
void InspectorDOMAgent::EnableAndReset() {
  enabled_.Set(true);
  history_ = MakeGarbageCollected<InspectorHistory>();
  dom_editor_ = MakeGarbageCollected<DOMEditor>(history_.Get());
  document_ = inspected_frames_->Root()->GetDocument();
  instrumenting_agents_->AddInspectorDOMAgent(this);
}
```

`DOMEditor` (`dom_editor.cc:48-478`) wraps each atomic DOM operation in an `InspectorHistory::Action` subclass:
- `InsertBeforeAction` (undo: `RemoveChild`)
- `RemoveChildAction` (undo: `InsertBefore`)
- `SetAttributeAction` (undo: restore old value or remove)
- `RemoveAttributeAction` (undo: `SetAttribute`)
- `SetNodeValueAction` (undo: restore old value)
- `SetOuterHTMLAction` (uses `DOMPatchSupport` for diff-based patch)
- `ReplaceChildAction`

For a scraper, you don't care about undo — but the `DOMEditor` is significant because every successful `DOM.setAttributeValue` call goes through it, which means **the page's MutationObserver fires** for every CDP-driven mutation.

### 4.1.3 The `DOM.getDocument` Entry Point

```cpp
// inspector_dom_agent.cc:767-786
protocol::Response InspectorDOMAgent::getDocument(
    std::optional<int> depth,
    std::optional<bool> pierce,
    std::unique_ptr<protocol::DOM::Node>* root) {
  if (!enabled_.Get()) enable(std::nullopt);     // backward-compat auto-enable
  if (!document_) return protocol::Response::ServerError("Document is not available");

  DiscardFrontendBindings();                    // wipes all node-id mappings

  int sanitized_depth = depth.value_or(2);       // default depth = 2
  if (sanitized_depth == -1) sanitized_depth = INT_MAX;

  *root = BuildObjectForNode(document_.Get(), sanitized_depth,
                             pierce.value_or(false),
                             document_node_to_id_map_.Get());
  return protocol::Response::Success();
}
```

**For scraping**: pass `depth: -1, pierce: true` to get the full tree including all iframes/shadow roots in one call. Default `depth=2` returns only the document + html + body.

### 4.1.4 The CDP Node Object

```json
{
  "nodeId": 1,
  "parentId": 0,                          // 0 = root, omitted if no parent
  "backendNodeId": 12345,                 // stable across navigations (for HAR)
  "nodeType": 9,                          // 1=Element, 3=Text, 8=Comment, 9=Document, 10=Doctype, 11=Fragment
  "nodeName": "#document",
  "localName": "",                        // for elements: lowercase tag name
  "nodeValue": "",                        // for text/comment nodes: the text
  "attributes": [],                       // flat array: [name1, value1, name2, value2, ...]
  "children": [/* ... */],                 // recursive
  "childNodeCount": 2,
  
  // Optional fields based on node type:
  "frameId": "A1B2C3...",                 // only for iframe elements
  "contentDocument": {/* Node */},         // only for iframe elements with pierce:true
  "shadowRoots": [/* Node */],            // only for elements with shadow DOM
  "shadowRootType": "open",                // "open" | "closed" | "user-agent"
  "pseudoElements": [/* Node */],         // ::before, ::after, ::marker, etc.
  "pseudoType": "before",
  
  "publicId": "",                         // for DOCTYPE
  "systemId": "",
  "internalSubset": "",
  
  "xmlVersion": "",                       // for XML documents
  "name": "",                             // for processing instructions
  "value": "",
  
  "baseURL": "https://example.com/",     // for documents
  
  "documentURL": "https://example.com/page"
}
```

---

## 4.2 The Complete DOM Domain API

### 4.2.1 The Command Reference Table

| Command | Implementation | What it does |
|---|---|---|
| `DOM.enable` | `:739` | Enable DOM agent. Optional `includeWhitespace: "all" \| "none"` |
| `DOM.disable` | (reverse) | Disable DOM agent |
| `DOM.getDocument` | `:767` | Get the root document node. `depth` (default 2, -1=full), `pierce` (default false) |
| `DOM.requestChildNodes` | `:974` | Request children of a node (lazy expansion). `depth`, `pierce` |
| `DOM.querySelector` | `:992` | `querySelector` on a node. Returns nodeId (0 if not found) |
| `DOM.querySelectorAll` | `:1015` | `querySelectorAll` on a node. Returns nodeId array |
| `DOM.getNodeForLocation` | `:3186` | Hit-test a (x, y) coordinate. Returns nodeId + related node info |
| `DOM.describeNode` | `:3281` | Get a Node object without binding it (no nodeId allocation) |
| `DOM.resolveNode` | `:1908` | Convert nodeId → `Runtime.RemoteObject` |
| `DOM.setAttributeValue` | `:1123` | Set a single attribute |
| `DOM.setAttributesAsText` | `:1167` | Parse a string like `class="foo" data-x="1"` and apply all attributes |
| `DOM.removeAttribute` | `:1216` | Remove an attribute |
| `DOM.removeNode` | `:1229` | Remove a node from its parent |
| `DOM.setNodeName` | `:1242` | Rename a node (creates new element, copies attrs, moves children) |
| `DOM.setNodeValue` | `:1371` | Set text content of a text/PI node |
| `DOM.getOuterHTML` | `:1310` | Get outer HTML of a node |
| `DOM.setOuterHTML` | `:1331` | Set outer HTML (uses DOMPatchSupport for diff-based patch) |
| `DOM.insertBefore` | `:1271` | Insert a node before a sibling |
| `DOM.moveNode` | `:1290` | Move a node to a new parent |
| `DOM.copyTo` | `:1259` | Copy a node to a new parent |
| `DOM.focus` | `:1771` | Focus an element (triggers `focus` event) |
| `DOM.scrollIntoViewIfNeeded` | `:3300` | Scroll element into view if not visible |
| `DOM.setFileInputFiles` | `:1789` | Set files on `<input type=file>` (file upload automation!) |
| `DOM.getBoxModel` | (in CSSAgent) | Get the box model (content/padding/border/margin quads) |
| `DOM.getFrameOwner` | `:3343` | Find the iframe element that owns a given frameId |
| `DOM.getFileInfo` | `:3387` | Get file info for a `<input type=file>` |
| `DOM.getSearchResults` | (search) | Search the DOM tree by string |
| `DOM.performSearch` | (search) | Start a search; returns searchId + resultCount |
| `DOM.getSearchResults` | (search) | Get paginated search results |
| `DOM.discardSearchResults` | (search) | Discard a search |
| `DOM.collectClassNamesFromSubtree` | `:3344` | Get all CSS class names used in a subtree |
| `DOM.getTopLayerElements` | `:3411` | Get top-layer elements (dialog, popover) |
| `DOM.getAnchorElement` | `:3434` | Get the anchor element for a target |
| `DOM.getDetachedDomNodes` | `:3466` | Get detached DOM nodes (memory leak detection) |

### 4.2.2 The Mutation Event Reference

| Event | When Fired | Payload |
|---|---|---|
| `DOM.setChildNodes` | First time children are requested for a node | `parentId`, `nodes[]` |
| `DOM.attributeModified` | An attribute was added or changed | `nodeId`, `name`, `value` |
| `DOM.attributeRemoved` | An attribute was removed | `nodeId`, `name` |
| `DOM.characterDataModified` | Text node content changed | `nodeId`, `newValue` |
| `DOM.childNodeCountUpdated` | A node's child count changed (children not yet expanded) | `nodeId`, `childNodeCount` |
| `DOM.childNodeInserted` | A child was inserted (children already expanded) | `parentNodeId`, `previousNodeId`, `node` |
| `DOM.childNodeRemoved` | A child was removed | `parentNodeId`, `nodeId` |
| `DOM.inlineStyleInvalidated` | Inline `style` attribute changed (batched) | `nodeIds[]` |
| `DOM.shadowRootPushed` | A shadow root was attached to an element | `hostId`, `root` |
| `DOM.shadowRootPopped` | A shadow root was detached | `hostId`, `rootId` |
| `DOM.pseudoElementAdded` | A pseudo-element was created | `parentId`, `pseudoElement` |
| `DOM.pseudoElementRemoved` | A pseudo-element was destroyed | `parentId`, `pseudoElementId` |
| `DOM.distributedNodesUpdated` | Slot distribution changed | `slotId`, `distributedNodes[]` |
| `DOM.adoptedStyleSheetsModified` | `document.adoptedStyleSheets` changed | `nodeId`, `frameId` |
| `DOM.documentUpdated` | The entire document was replaced (navigation) | (none) — must re-call `getDocument` |
| `DOM.topLayerElementsUpdated` | Top-layer (dialog/popover) changed | (none) |
| `DOM.scrollableFlagUpdated` | `overflow` style changed scrollability | `nodeId`, `isScrollable` |

### 4.2.3 How Mutations Are Captured — NOT via MutationObserver

**Important clarification**: Chromium does **NOT** use the JavaScript `MutationObserver` API for DevTools. It uses baked-in **C++ instrumentation hooks ("probes")** in the DOM mutation code paths.

The instrumentation entry points on the agent are listed at `inspector_dom_agent.h:291-321`:

```cpp
// Methods called from the InspectorInstrumentation.
void DomContentLoadedEventFired(LocalFrame*);
void DidCommitLoad(LocalFrame*, DocumentLoader*);
void DidRestoreFromBackForwardCache(LocalFrame*);
void DidInsertDOMNode(Node*);
void WillRemoveDOMNode(Node*);
void WillModifyDOMAttr(Element*, const AtomicString& old_value, const AtomicString& new_value);
void DidModifyDOMAttr(Element*, const QualifiedName&, const AtomicString& value);
void DidRemoveDOMAttr(Element*, const QualifiedName&);
void StyleAttributeInvalidated(const HeapVector<Member<Element>>&);
void DidModifyAdoptedStyleSheets(Node*);
void AdoptedStyleSheetsInvalidated(Node*);
void CharacterDataModified(CharacterData*);
void DidInvalidateStyleAttr(Element*);
void DidPushShadowRoot(Element* host, ShadowRoot*);
void WillPopShadowRoot(Element* host, ShadowRoot*);
void DidPerformSlotDistribution(HTMLSlotElement*);
void FrameDocumentUpdated(LocalFrame*);
void FrameOwnerContentUpdated(LocalFrame*, HTMLFrameOwnerElement*);
void PseudoElementCreated(PseudoElement*);
void TopLayerElementsChanged();
void PseudoElementDestroyed(PseudoElement*);
void NodeCreated(Node* node);
void UpdateScrollableFlag(Node* node, std::optional<bool>);
```

These are called from blink's `ContainerNode::InsertChild`, `ContainerNode::RemoveChild`, `Element::setAttribute`, `CharacterData::setData`, `Element::AttachShadow`, `Element::DetachShadow`, etc.

### 4.2.4 `childNodeInserted` Implementation

```cpp
// inspector_dom_agent.cc:2795-2826
void InspectorDOMAgent::DidInsertDOMNode(Node* node) {
  InspectorDOMAgent::IncludeWhitespaceEnum include_whitespace = IncludeWhitespace();
  if (ShouldSkipNode(node, include_whitespace)) return;

  // We could be attaching existing subtree. Forget the bindings.
  Unbind(node);

  ContainerNode* parent = node->parentNode();
  if (!parent) return;
  // Return if parent is not mapped yet.
  int parent_id = BoundNodeId(parent);
  if (!parent_id) return;

  if (!children_requested_.Contains(parent_id)) {
    // No children are mapped yet -> only notify on changes of child count.
    auto it = cached_child_count_.find(parent_id);
    int count = (it != cached_child_count_.end() ? it->value : 0) + 1;
    cached_child_count_.Set(parent_id, count);
    GetFrontend()->childNodeCountUpdated(parent_id, count);     // <-- event 1
  } else {
    // Children have been requested -> return value of a new child.
    Node* prev_sibling = InnerPreviousSibling(node, include_whitespace);
    int prev_id = prev_sibling ? BoundNodeId(prev_sibling) : 0;
    std::unique_ptr<protocol::DOM::Node> value =
        BuildObjectForNode(node, 0, false, document_node_to_id_map_.Get());
    GetFrontend()->childNodeInserted(parent_id, prev_id, std::move(value)); // <-- event 2
  }
}
```

Two distinct behaviors depending on whether the frontend has already expanded the parent:

- **Parent NOT yet expanded**: only `DOM.childNodeCountUpdated(parentId, newCount)` is emitted. The actual child node isn't sent — the frontend just updates the expand-arrow badge count.
- **Parent already expanded**: `DOM.childNodeInserted(parentNodeId, previousNodeId, node)` is emitted with a freshly-built protocol `Node` object. The frontend inserts it into its mirror tree at the correct position relative to `previousNodeId`.

### 4.2.5 `childNodeRemoved` Implementation

```cpp
// inspector_dom_agent.cc:2828-2849
void InspectorDOMAgent::DOMNodeRemoved(Node* node) {
  ContainerNode* parent = node->parentNode();
  int parent_id = BoundNodeId(parent);
  if (!parent_id) return;

  if (!children_requested_.Contains(parent_id)) {
    int count = cached_child_count_.at(parent_id) - 1;
    cached_child_count_.Set(parent_id, count);
    GetFrontend()->childNodeCountUpdated(parent_id, count);
  } else {
    GetFrontend()->childNodeRemoved(parent_id, BoundNodeId(node));
  }
  Unbind(node);    // recursively unbinds subtree + shadow roots + iframes + pseudo-elements
}
```

`Unbind` (`:581-622`) recurses into shadow roots, iframe content documents, and pseudo-elements — so a single `childNodeRemoved` can free many node IDs.

### 4.2.6 `attributeModified` / `attributeRemoved`

```cpp
// inspector_dom_agent.cc:2853-2888
void InspectorDOMAgent::WillModifyDOMAttr(Element*, const AtomicString& old_value,
                                           const AtomicString& new_value) {
  suppress_attribute_modified_event_ = (old_value == new_value);   // no-op suppression
}

void InspectorDOMAgent::DidModifyDOMAttr(Element* element, const QualifiedName& name,
                                         const AtomicString& value) {
  bool should_suppress_event = suppress_attribute_modified_event_;
  suppress_attribute_modified_event_ = false;
  if (should_suppress_event) return;

  int id = BoundNodeId(element);
  if (!id) return;
  NotifyDidModifyDOMAttr(element);    // tells InspectorCSSAgent etc.
  GetFrontend()->attributeModified(id, name.ToString(), value);
}

void InspectorDOMAgent::DidRemoveDOMAttr(Element* element, const QualifiedName& name) {
  int id = BoundNodeId(element);
  if (!id) return;
  NotifyDidModifyDOMAttr(element);
  GetFrontend()->attributeRemoved(id, name.ToString());
}
```

### 4.2.7 `inlineStyleInvalidated` (Batched)

Style attribute changes are coalesced through `InspectorRevalidateDOMTask` (`:156-218`), a timer-based batcher:

```cpp
// inspector_dom_agent.cc:2914-2928
void InspectorDOMAgent::StyleAttributeInvalidated(
    const HeapVector<Member<Element>>& elements) {
  auto node_ids = std::make_unique<protocol::Array<int>>();
  for (unsigned i = 0, size = elements.size(); i < size; ++i) {
    Element* element = elements.at(i);
    int id = BoundNodeId(element);
    if (!id) continue;
    NotifyDidModifyDOMAttr(element);
    node_ids->emplace_back(id);
  }
  GetFrontend()->inlineStyleInvalidated(std::move(node_ids));
}
```

The frontend must then call `CSS.getInlineStylesForNode` to fetch the new style — Chromium does **not** push the new style text in the event (saves bandwidth).

### 4.2.8 `characterDataModified`

```cpp
// inspector_dom_agent.cc:2930-2948
void InspectorDOMAgent::CharacterDataModified(CharacterData* character_data) {
  int id = BoundNodeId(character_data);
  if (id && ShouldSkipNode(character_data, IncludeWhitespace())) {
    DOMNodeRemoved(character_data);    // text became whitespace-only and we're filtering
    return;
  }
  if (!id) {
    DidInsertDOMNode(character_data);  // text node is brand new
    return;
  }
  GetFrontend()->characterDataModified(id, character_data->data());
}
```

---

## 4.3 Shadow DOM Traversal

### 4.3.1 The Three Shadow Root Types

```cpp
// inspector_dom_agent.cc:2367-2378
protocol::DOM::ShadowRootType InspectorDOMAgent::GetShadowRootType(ShadowRoot* shadow_root) {
  switch (shadow_root->GetMode()) {
    case ShadowRootMode::kUserAgent: return protocol::DOM::ShadowRootTypeEnum::UserAgent;
    case ShadowRootMode::kOpen:      return protocol::DOM::ShadowRootTypeEnum::Open;
    case ShadowRootMode::kClosed:   return protocol::DOM::ShadowRootTypeEnum::Closed;
  }
  NOTREACHED();
}
```

**Critical fact for scraping**: **Closed shadow roots ARE returned to DevTools clients** — the `Open`/`Closed` distinction is a JS-level encapsulation only; the DevTools protocol has backdoor access to both. (`UserAgent` shadow roots are also returned but `AssertEditableElement` (`:716-730`) refuses to edit them.)

### 4.3.2 How Shadow Roots Are Represented

In `BuildObjectForNode` (`:2407-2500`), shadow roots are emitted as a separate array on the host element:

```cpp
// inspector_dom_agent.cc:2456-2463
if (ShadowRoot* root = element->GetShadowRoot()) {
  auto shadow_roots = std::make_unique<protocol::Array<protocol::DOM::Node>>();
  shadow_roots->emplace_back(BuildObjectForNode(
      root, pierce ? depth : 0, pierce, nodes_map, flatten_result));
  value->setShadowRoots(std::move(shadow_roots));
  force_push_children = true;
}
```

The shadow root node itself carries a `shadowRootType` discriminator:

```cpp
// inspector_dom_agent.cc:2511-2518
} else if (auto* shadow_root = DynamicTo<ShadowRoot>(node)) {
  value->setShadowRootType(GetShadowRootType(shadow_root));
}
```

### 4.3.3 `shadowRootPushed` / `shadowRootPopped` Events

```cpp
// inspector_dom_agent.cc:2977-3000
void InspectorDOMAgent::DidPushShadowRoot(Element* host, ShadowRoot* root) {
  if (!host->ownerDocument()) return;
  int host_id = BoundNodeId(host);
  if (!host_id) return;

  PushChildNodesToFrontend(host_id, 1);
  GetFrontend()->shadowRootPushed(
      host_id,
      BuildObjectForNode(root, 0, false, document_node_to_id_map_.Get()));
}

void InspectorDOMAgent::WillPopShadowRoot(Element* host, ShadowRoot* root) {
  if (!host->ownerDocument()) return;
  int host_id = BoundNodeId(host);
  int root_id = BoundNodeId(root);
  if (host_id && root_id)
    GetFrontend()->shadowRootPopped(host_id, root_id);
}
```

### 4.3.4 Traversing Shadow Boundaries

`InnerParentNode` (`:2693-2698`) crosses the shadow boundary upward:

```cpp
// inspector_dom_agent.cc:2693-2698
Node* InspectorDOMAgent::InnerParentNode(Node* node) {
  if (auto* document = DynamicTo<Document>(node)) {
    return document->LocalOwner();        // iframe owner element
  }
  return node->ParentOrShadowHostNode();  // jumps from shadow root to host
}
```

`dom_traversal_utils.cc` provides a separate `FlatTreeTraversal`-based API for flat-tree traversal (used by `InspectorDOMSnapshotAgent`):

```cpp
// dom_traversal_utils.cc:10-19
Node* FirstChild(const Node& node, bool include_user_agent_shadow_tree) {
  DCHECK(include_user_agent_shadow_tree || !node.IsInUserAgentShadowRoot());
  if (!include_user_agent_shadow_tree) {
    ShadowRoot* shadow_root = node.GetShadowRoot();
    if (shadow_root && shadow_root->GetMode() == ShadowRootMode::kUserAgent) {
      return node.firstChild();            // skip UA shadow tree
    }
  }
  return FlatTreeTraversal::FirstChild(node);
}
```

When `pierce: true` is passed to `getDocument` / `requestChildNodes` / `collectClassNamesFromSubtree`, `CollectNodes` (`:2703-2734`) recurses into BOTH iframe content documents AND shadow roots:

```cpp
// inspector_dom_agent.cc:2703-2734
void InspectorDOMAgent::CollectNodes(
    Node* node, int depth, bool pierce,
    InspectorDOMAgent::IncludeWhitespaceEnum include_whitespace,
    base::RepeatingCallback<bool(Node*)> filter,
    HeapVector<Member<Node>>* result) {
  if (filter && filter.Run(node)) result->push_back(node);
  if (--depth <= 0) return;

  auto* element = DynamicTo<Element>(node);
  if (pierce && element) {
    if (auto* frame_owner = DynamicTo<HTMLFrameOwnerElement>(node)) {
      if (frame_owner->ContentFrame() && frame_owner->ContentFrame()->IsLocalFrame()) {
        if (Document* doc = frame_owner->contentDocument())
          CollectNodes(doc, depth, pierce, include_whitespace, filter, result);
      }
    }
    ShadowRoot* root = element->GetShadowRoot();
    if (pierce && root)
      CollectNodes(root, depth, pierce, include_whitespace, filter, result);
  }
  for (Node* child = InnerFirstChild(node, include_whitespace); child;
       child = InnerNextSibling(child, include_whitespace)) {
    CollectNodes(child, depth, pierce, include_whitespace, filter, result);
  }
}
```

---

## 4.4 Iframe Handling

### 4.4.1 How Iframes Are Represented

An iframe is exposed in the DOM tree as a regular `Element` node of type `iframe` (or `frame`), with two extra fields populated by `BuildObjectForNode`:

```cpp
// inspector_dom_agent.cc:2442-2454
if (auto* frame_owner = DynamicTo<HTMLFrameOwnerElement>(node)) {
  if (frame_owner->ContentFrame()) {
    value->setFrameId(
        IdentifiersFactory::FrameId(frame_owner->ContentFrame()));
  }
  if (Document* doc = frame_owner->contentDocument()) {
    value->setContentDocument(BuildObjectForNode(
        doc, pierce ? depth : 0, pierce, nodes_map, flatten_result));
  }
}
```

So the protocol `Node` for an `<iframe>` looks like:

```json
{
  "nodeId": 12,
  "nodeType": 1,
  "nodeName": "IFRAME",
  "attributes": ["src","https://example.com/"],
  "frameId": "A1B2C3…",          // matches Page.frameNavigated.frame.id
  "contentDocument": {            // only present if pierce:true
    "nodeId": 13,
    "nodeType": 9,                // Document
    "documentURL": "https://example.com/",
    "children": [/* … */]
  }
}
```

Without `pierce: true`, the iframe appears as a normal element with `frameId` set but **no** `contentDocument`. The frontend then attaches a separate inspector session to that frame's renderer (in OOPIF scenarios) using `Target.attachToTarget`, OR (same-process) accesses it via the same DOM agent with `pierce: true`.

### 4.4.2 Cross-Process vs Same-Process Iframes

| Iframe Type | Process | DevTools Behavior |
|---|---|---|
| Same-origin iframe | Same renderer process | `pierce: true` recurses into it via `frame_owner->contentDocument()` |
| Cross-origin OOPIF | Separate renderer process | `pierce: true` returns `frameId` but `contentDocument` may be null. Must use `Target.attachToTarget` to inspect the OOPIF separately |
| Cross-origin iframe with `frame-ancestors` blocking | N/A (blocked) | Won't render at all |
| `srcdoc` iframe | Same process | Treated as same-origin |

### 4.4.3 `DOM.getFrameOwner` — Reverse Lookup

```cpp
// inspector_dom_agent.cc:3343-3385
protocol::Response InspectorDOMAgent::getFrameOwner(
    const String& frame_id, int* backend_node_id,
    std::optional<int>* node_id) {
  Frame* found_frame = nullptr;
  for (Frame* frame = inspected_frames_->Root(); frame;
       frame = frame->Tree().TraverseNext(inspected_frames_->Root())) {
    if (IdentifiersFactory::FrameId(frame) == frame_id) {
      found_frame = frame;
      break;
    }
    // also walks fenced frames...
  }
  if (!found_frame) return protocol::Response::ServerError(
      "Frame with the given id was not found.");
  auto* frame_owner = DynamicTo<HTMLFrameOwnerElement>(found_frame->Owner());
  if (!frame_owner) return protocol::Response::ServerError(
      "Frame with the given id does not belong to the target.");
  *backend_node_id = IdentifiersFactory::IntIdForNode(frame_owner);
  if (enabled_.Get() && document_ && BoundNodeId(document_))
    *node_id = PushNodePathToFrontend(frame_owner);
  return protocol::Response::Success();
}
```

### 4.4.4 `InvalidateFrameOwnerElement` (Re-push on Navigation)

When the iframe navigates (cross-document), the agent fires the `InvalidateFrameOwnerElement` flow (`:2746-2766`) which simulates a `childNodeRemoved` + `childNodeInserted` for the frame owner element so the frontend re-fetches its content document:

```cpp
// inspector_dom_agent.cc:2746-2766
void InspectorDOMAgent::InvalidateFrameOwnerElement(HTMLFrameOwnerElement* frame_owner) {
  if (!frame_owner) return;
  int frame_owner_id = BoundNodeId(frame_owner);
  if (!frame_owner_id) return;

  int parent_id = BoundNodeId(InnerParentNode(frame_owner));
  GetFrontend()->childNodeRemoved(parent_id, frame_owner_id);
  Unbind(frame_owner);

  std::unique_ptr<protocol::DOM::Node> value =
      BuildObjectForNode(frame_owner, 0, false, document_node_to_id_map_.Get());
  Node* previous_sibling = InnerPreviousSibling(frame_owner, IncludeWhitespace());
  int prev_id = previous_sibling ? BoundNodeId(previous_sibling) : 0;
  GetFrontend()->childNodeInserted(parent_id, prev_id, std::move(value));
}
```

### 4.4.5 `InspectedFrames` Iterator

`InspectedFrames` (`inspected_frames.cc`) is the iterator over all local frames reachable from the root frame within the same process (probe-sink equivalence):

```cpp
// inspected_frames.cc:47-65
InspectedFrames::Iterator& InspectedFrames::Iterator::operator++() {
  if (!current_) return *this;
  Frame* frame = current_->Tree().TraverseNext(root_);
  current_ = nullptr;
  for (; frame; frame = frame->Tree().TraverseNext(root_)) {
    auto* local = DynamicTo<LocalFrame>(frame);
    if (!local) continue;
    if (local->GetProbeSink() == root_->GetProbeSink()) {
      current_ = local;
      break;
    }
  }
  return *this;
}
```

Same-process frames share the same `ProbeSink` so they're visited; OOPIFs do not, so they require a separate DevTools target.

---

## 4.5 CSS Domain

### 4.5.1 The InspectorCSSAgent

`InspectorCSSAgent` (`inspector_css_agent.cc`, 5,425 lines) is the largest single agent. Key methods:

### 4.5.2 `CSS.enable`

```cpp
// inspector_css_agent.cc:916-927
void InspectorCSSAgent::enable(std::unique_ptr<EnableCallback> prp_callback) {
  if (!dom_agent_->Enabled()) {
    prp_callback->sendFailure(protocol::Response::ServerError(
        "DOM agent needs to be enabled first."));
    return;
  }
  enable_requested_.Set(true);
  resource_content_loader_->EnsureResourcesContentLoaded(
      resource_content_loader_client_id_,
      BindOnce(&InspectorCSSAgent::ResourceContentLoaded, WrapPersistent(this),
               std::move(prp_callback)));
}
```

Note: enabling CSS is **asynchronous** because it first ensures all stylesheet resources have been re-fetched (via `InspectorResourceContentLoader`) so that the agent has source text for every stylesheet.

### 4.5.3 `CSS.getComputedStyleForNode`

```cpp
// inspector_css_agent.cc:2397-2452
protocol::Response InspectorCSSAgent::getComputedStyleForNode(
    int node_id,
    std::unique_ptr<protocol::Array<protocol::CSS::CSSComputedStyleProperty>>* style,
    std::unique_ptr<protocol::CSS::ComputedStyleExtraFields>* extra_fields) {
  // ... assertions ...
  element->GetDocument().UpdateStyleAndLayoutForNode(
      element, DocumentUpdateReason::kInspector);     // forces layout
  auto* computed_style_info =
      MakeGarbageCollected<CSSComputedStyleDeclaration>(element, true);
  CSSComputedStyleDeclaration::ScopedCleanStyleForAllProperties
      clean_style_scope(computed_style_info);
  *style = std::make_unique<protocol::Array<protocol::CSS::CSSComputedStyleProperty>>();
  for (CSSPropertyID property_id : CSSPropertyIDList()) {
    const CSSProperty& property_class =
        CSSProperty::Get(ResolveCSSPropertyID(property_id));
    if (!property_class.IsWebExposed(element->GetExecutionContext()) ||
        property_class.IsShorthand() || !property_class.IsProperty()) continue;
    (*style)->emplace_back(
        protocol::CSS::CSSComputedStyleProperty::create()
            .setName(property_class.GetPropertyNameString())
            .setValue(computed_style_info->GetPropertyValue(property_id))
            .build());
  }
  for (const auto& it : computed_style_info->GetVariables()) {     // CSS vars too
    (*style)->emplace_back(protocol::CSS::CSSComputedStyleProperty::create()
                               .setName(it.key)
                               .setValue(it.value->CssText())
                               .build());
  }
  // ...
}
```

Forces layout (`UpdateStyleAndLayoutForNode`) and walks every CSS property in `CSSPropertyIDList()`. Includes CSS custom properties (variables).

### 4.5.4 `CSS.getMatchedStylesForNode` — The Most Thorough

(inspector_css_agent.cc:1461-1729) Returns a parallel structure:

- `inlineStyle` — the element's `style="…"` attribute as an `InspectorStyleSheetForInlineStyle`.
- `attributesStyle` — style implied by `align`, `bgcolor`, `clear`, etc. (presentation attributes).
- `matchedCSSRules` — rules in document order whose selector matches.
- `inheritedEntries` — chain of inlineStyle + matched rules from each ancestor (CSS inheritance).
- `pseudoIdMatches` — `::before`, `::after`, `::marker`, `::backdrop`, etc. rules.
- `inheritedPseudoIdMatches` — inherited pseudo-element rules.
- `cssKeyframesRules` — `@keyframes` rules referenced by `animation-name`.
- `cssPositionTryRules` — `@position-try` rules.
- `cssPropertyRules` — `@property` rules.
- `cssPropertyRegistrations` — registered custom properties.
- `cssAtRules` — `@font-face` / `counter-style` rules.
- `cssFunctionRules` — `@function` rules (new CSS Function proposal).
- `parentLayoutNodeId` — for layout inheritance tracking.

Implementation uses `InspectorStyleResolver` (the blink-internal style cascade re-run for one element) plus `InspectorGhostRules` (synthetic rules injected to mirror `:is()`, `:where()`, etc. expansion). This is the only way to get the **full effective cascade** for a node.

### 4.5.5 `CSS.setStyleTexts` (Batch Edit)

```cpp
// inspector_css_agent.cc:3081-3137
protocol::Response InspectorCSSAgent::setStyleTexts(
    std::unique_ptr<protocol::Array<protocol::CSS::StyleDeclarationEdit>> edits,
    std::optional<int> node_for_property_syntax_validation,
    std::unique_ptr<protocol::Array<protocol::CSS::CSSStyle>>* result) {
  FrontendOperationScope scope;
  HeapVector<Member<StyleSheetAction>> actions;
  protocol::Response response = MultipleStyleTextsActions(std::move(edits), &actions);
  if (!response.IsSuccess()) return response;
  // ... perform actions, with manual rollback on failure ...
  int n = actions.size();
  auto serialized_styles = std::make_unique<protocol::Array<protocol::CSS::CSSStyle>>();
  for (int i = 0; i < n; ++i) {
    Member<StyleSheetAction> action = actions.at(i);
    bool success = action->Perform(exception_state);
    if (!success) {
      for (int j = i - 1; j >= 0; --j) {       // rollback earlier actions
        Member<StyleSheetAction> revert = actions.at(j);
        DummyExceptionStateForTesting undo_exception_state;
        revert->Undo(undo_exception_state);
        DCHECK(!undo_exception_state.HadException());
      }
      return protocol::Response::ServerError(...);
    }
  }
  // ... append performed actions to history (for undo) ...
}
```

Atomic batch: if any edit fails, all earlier edits in the batch are rolled back.

### 4.5.6 `CSS.createStyleSheet` (Inject a New Stylesheet)

```cpp
// inspector_css_agent.cc:3360-3383
protocol::Response InspectorCSSAgent::createStyleSheet(
    const String& frame_id, std::optional<bool> force,
    protocol::DOM::StyleSheetId* out_style_sheet_id) {
  LocalFrame* frame = IdentifiersFactory::FrameById(inspected_frames_, frame_id);
  if (!frame) return protocol::Response::ServerError("Frame not found");
  Document* document = frame->GetDocument();
  if (!document) return protocol::Response::ServerError("Frame does not have a document");

  InspectorStyleSheet* inspector_style_sheet =
      CreateViaInspectorStyleSheet(document, force.value_or(false));
  if (!inspector_style_sheet)
    return protocol::Response::ServerError("No target stylesheet found");
  UpdateActiveStyleSheets(document);
  *out_style_sheet_id = inspector_style_sheet->Id();
  return protocol::Response::Success();
}
```

`CreateViaInspectorStyleSheet` creates a synthetic `<style>` element appended to the document (so it shows up in the live DOM after `UpdateActiveStyleSheets`).

### 4.5.7 `CSS.addRule`

```cpp
// inspector_css_agent.cc:3384-3426
protocol::Response InspectorCSSAgent::addRule(
    const String& style_sheet_id, const String& rule_text,
    std::unique_ptr<protocol::CSS::SourceRange> location,
    std::optional<int> node_for_property_syntax_validation,
    std::unique_ptr<protocol::CSS::CSSRule>* result) {
  // ...
  AddRuleAction* action = MakeGarbageCollected<AddRuleAction>(
      inspector_style_sheet, rule_text, rule_location);
  bool success = dom_agent_->History()->Perform(action, exception_state);
  // ...
  CSSStyleRule* rule = action->TakeRule();
  *result = BuildObjectForRule(rule, element);
  return protocol::Response::Success();
}
```

`AddRuleAction` is another `InspectorHistory::Action`, so undo restores the previous stylesheet text.

---

## 4.6 DOMStorage (localStorage / sessionStorage)

### 4.6.1 The Protocol Surface

Defined in `third_party/blink/public/devtools_protocol/domains/DOMStorage.pdl`:

```pdl
experimental domain DOMStorage
  type SerializedStorageKey extends string
  type StorageId extends object
    properties
      optional string securityOrigin
      optional SerializedStorageKey storageKey
      boolean isLocalStorage
  type Item extends array of string
  command clear      parameters StorageId storageId
  command disable
  command enable
  command getDOMStorageItems  parameters StorageId storageId
                              returns array of Item entries
  command removeDOMStorageItem parameters StorageId storageId, string key
  command setDOMStorageItem   parameters StorageId storageId, string key, string value
  event domStorageItemAdded   parameters StorageId storageId, string key, string newValue
  event domStorageItemRemoved parameters StorageId storageId, string key
  event domStorageItemUpdated parameters StorageId storageId, string key, string oldValue, string newValue
  event domStorageItemsCleared parameters StorageId storageId
```

### 4.6.2 StorageId Format

The `StorageId` carries **both** `securityOrigin` (deprecated, pre-StorageKeys) and `storageKey` (modern, partition-aware) — at least one must be set; `isLocalStorage` is always required. For a same-origin scraping scenario:

```json
{ "securityOrigin": "https://example.com", "isLocalStorage": true }
```

For storage-partitioned scenarios (e.g. when an iframe uses a different storage key than its origin due to network isolation):

```json
{ "storageKey": "{\"origin\":\"https://example.com\",\"top_level_site\":\"https://example.com\",\"nonce\":null,\"has_cross_site_ancestor\":false}", "isLocalStorage": false }
```

### 4.6.3 How DOMStorage Observes Changes

The agent lives at `third_party/blink/renderer/core/inspector/inspector_dom_storage_agent.{cc,h}` (not in your slice). It:

1. Maintains a map of `(StorageId → StorageArea*)` obtained via `StorageNamespace::LocalStorageArea()` / `SessionStorageArea()`.
2. Registers itself as a `StorageAreaObserver` (mojo interface) on each tracked area.
3. On `enable()`, does nothing eagerly — items are fetched lazily on `getDOMStorageItems`.
4. On any mojo callback from the storage area (`KeyAdded`, `KeyRemoved`, `KeyValueChange`, `AllRemoved`), emits the corresponding CDP event (`domStorageItemAdded` / `domStorageItemRemoved` / `domStorageItemUpdated` / `domStorageItemsCleared`).

### 4.6.4 How to Enumerate All Storage Across Frames

For a scraping scenario where you need ALL localStorage and sessionStorage across the entire page (including iframes):

```cpp
// 1. Get all frames via Page.getResourceTree or Page.getFrameTree
// 2. For each frame's securityOrigin (and both isLocalStorage true/false):
//    a. Call DOMStorage.getDOMStorageItems
//    b. Listen for change events
```

### 4.6.5 OOPIF Storage Partitioning

OOPIFs (cross-origin iframes) have their own storage partition. To watch their localStorage you must:
1. Attach a separate DevTools target via `Target.attachToTarget`
2. Re-do the DOMStorage.enable + getDOMStorageItems on that target
3. Listen for events on that target's session

For modern storage-partitioned scenarios (e.g. with network isolation), pass `storageKey` instead of `securityOrigin` — obtainable via `Storage.getStorageKeyForFrame(frameId)` (browser-side).

---

## 4.7 IndexedDB Monitoring

### 4.7.1 The Protocol Surface

The protocol domain is in `third_party/blink/public/devtools_protocol/domains/IndexedDB.pdl`. Full surface:

| Command | Returns |
|---|---|
| `IndexedDB.enable` | — |
| `IndexedDB.disable` | — |
| `IndexedDB.requestDatabaseNames` | `array of string databaseNames` |
| `IndexedDB.requestDatabase` | `DatabaseWithObjectStores databaseWithObjectStores` |
| `IndexedDB.requestData` | `array of DataEntry objectStoreDataEntries` + `boolean hasMore` |
| `IndexedDB.getMetadata` | `number entriesCount` + `number keyGeneratorValue` |
| `IndexedDB.clearObjectStore` | — |
| `IndexedDB.deleteObjectStoreEntries` | — |
| `IndexedDB.deleteDatabase` | — |

Every command takes **exactly one** of `securityOrigin`, `storageKey`, or `storageBucket` to identify the storage partition (validated server-side: "At least and at most one of securityOrigin, storageKey, or storageBucket must be specified.").

### 4.7.2 The DataEntry Shape

```pdl
type DataEntry extends object
  properties
    Runtime.RemoteObject key         # the entry's key (could be number, string, date, array)
    Runtime.RemoteObject primaryKey  # the entry's primary key (different from key when reading via an index)
    Runtime.RemoteObject value       # the value object
```

Each `DataEntry` carries three `Runtime.RemoteObject` handles — the inspector materializes the IDB records as V8 objects and serializes them through the normal RemoteObject mechanism (with `JSON` or `binary` depth wrappers). For large blobs/files inside IDB values, the agent truncates to avoid OOM.

### 4.7.3 The KeyRange Parameter

```pdl
type KeyRange extends object
  properties
    optional Key lower
    optional Key upper
    boolean lowerOpen
    boolean upperOpen
```

Pass a `KeyRange` to `requestData` to page through results: `skipCount` + `pageSize` for pagination, check `hasMore` in the response, increment `skipCount` by `pageSize`, repeat.

### 4.7.4 How the Agent Reads IDB

`InspectorIndexedDBAgent` (file missing from this slice) opens a `mojo::Remote<storage::mojom::IndexedDBControl>` to the storage service, calls `ForceOpen()`/`GetUsage()` then `OpenConnections()` to enumerate databases. For `requestData`, it uses the standard IDB connection API (`IDBDatabase::Open`, `IDBTransaction::ObjectStore`, `IDBObjectStore::OpenCursor`) running in the renderer process. The cursor walks `pageSize` records, serializing each via `v8::ObjectSerializer` (similar to `Runtime.evaluate` return serialization).

### 4.7.5 Browser-Side Storage Tracking

The browser-side `StorageHandler` emits `Storage.indexedDBListUpdated` / `Storage.indexedDBContentUpdated` events when you've called `Storage.trackIndexedDBForOrigin`. These are **change notifications only** — to actually read the data you must still call `IndexedDB.requestDatabase` / `requestData`. Use them together: track → on `IndexedDBContentUpdated` event → re-issue `requestData` to fetch the diff.

```cpp
// browser/devtools/protocol/storage_handler.cc:152-194 (excerpt)
class StorageHandler::CacheStorageObserver : storage::mojom::CacheStorageObserver {
 public:
  void OnCacheListChanged(const storage::BucketLocator& bucket_locator) override {
    auto found = storage_keys_.find(bucket_locator.storage_key);
    if (found == storage_keys_.end()) return;
    owner_->NotifyCacheStorageListChanged(bucket_locator);
  }
  void OnCacheContentChanged(const storage::BucketLocator& bucket_locator,
                             const std::string& cache_name) override {
    if (storage_keys_.find(bucket_locator.storage_key) == storage_keys_.end()) return;
    owner_->NotifyCacheStorageContentChanged(bucket_locator, cache_name);
  }
 private:
  base::flat_set<blink::StorageKey> storage_keys_;
  mojo::Receiver<storage::mojom::CacheStorageObserver> receiver_;
};
```

---

## 4.8 CacheStorage Monitoring (ServiceWorker Cache API)

### 4.8.1 The Protocol Surface

`third_party/blink/public/devtools_protocol/domains/CacheStorage.pdl`:

| Command | Returns |
|---|---|
| `CacheStorage.requestCacheNames` | `array of Cache caches` |
| `CacheStorage.requestEntries` | `array of DataEntry cacheDataEntries` + `number returnCount` |
| `CacheStorage.deleteCache` | — |
| `CacheStorage.deleteEntry` | — |
| `CacheStorage.requestCachedResponse` | `CachedResponse response` (body is `binary` = base64) |

### 4.8.2 CacheId / Cache / DataEntry Types

```pdl
type CacheId extends string
type Cache extends object
  properties
    CacheId cacheId
    string securityOrigin
    string storageKey
    optional Storage.StorageBucket storageBucket
    string cacheName
type DataEntry extends object
  properties
    string requestURL
    string requestMethod
    array of Header requestHeaders
    number responseTime          # seconds since epoch
    integer responseStatus
    string responseStatusText
    CachedResponseType responseType   # basic | cors | default | error | opaqueResponse | opaqueRedirect
    array of Header responseHeaders
type CachedResponse extends object
  properties
    binary body    # base64-encoded response body
```

### 4.8.3 Browser-Side Storage Tracking (in this slice)

The browser-side `StorageHandler` handles `Storage.trackCacheStorageForOrigin` / `trackCacheStorageForStorageKey` which subscribe to the `storage::mojom::CacheStorageObserver` Mojo interface:

```cpp
// storage_handler.cc:930-949
void StorageHandler::NotifyCacheStorageListChanged(
    const storage::BucketLocator& bucket_locator) {
  frontend_->CacheStorageListUpdated(
      bucket_locator.storage_key.origin().Serialize(),
      bucket_locator.storage_key.Serialize(),
      base::NumberToString(bucket_locator.id.value()));
}

void StorageHandler::NotifyCacheStorageContentChanged(
    const storage::BucketLocator& bucket_locator, const std::string& name) {
  frontend_->CacheStorageContentUpdated(
      bucket_locator.storage_key.origin().Serialize(),
      bucket_locator.storage_key.Serialize(),
      base::NumberToString(bucket_locator.id.value()), name);
}
```

### 4.8.4 Agent Implementation (in real Chromium — file missing from this slice)

`InspectorCacheStorageAgent` opens a `mojo::Remote<storage::mojom::CacheStorageControl>` and:
1. `requestCacheNames` → `cache_storage_control->OpenOriginalCacheStorage(storage_key, ...)` then `MatchAllCaches(...)` → returns the names.
2. `requestEntries` → `cache_storage->Open(cache_name)` then `cache->Keys(...)` to enumerate request URLs.
3. `requestCachedResponse` → `cache->Match(CacheStorageQueryParams, ...)` to load the response body, returned as base64.
4. `deleteCache` → `cache_storage_control->DeleteCache()`.
5. `deleteEntry` → `cache->Delete(CacheStorageQueryParams)`.

### 4.8.5 The `pathFilter` Parameter

`requestEntries` accepts an optional `pathFilter` string; only entries whose request URL contains the substring are returned. The protocol also returns `returnCount` (total count of entries in the cache, ignoring `pathFilter`).

---

## 4.9 File Locations Reference

| Component | File Path |
|---|---|
| InspectorDOMAgent | `third_party/blink/renderer/core/inspector/inspector_dom_agent.cc` + `.h` |
| DOMEditor | `third_party/blink/renderer/core/inspector/dom_editor.cc` + `.h` |
| DOMPatchSupport | `third_party/blink/renderer/core/inspector/dom_patch_support.cc` + `.h` |
| DOMTraversalUtils | `third_party/blink/renderer/core/inspector/dom_traversal_utils.cc` + `.h` |
| InspectedFrames | `third_party/blink/renderer/core/inspector/inspected_frames.cc` + `.h` |
| InspectorCSSAgent | `third_party/blink/renderer/core/inspector/inspector_css_agent.cc` + `.h` |
| InspectorCSSParserObserver | `third_party/blink/renderer/core/inspector/inspector_css_parser_observer.cc` + `.h` |
| InspectorDOMSnapshotAgent | `third_party/blink/renderer/core/inspector/inspector_dom_snapshot_agent.cc` + `.h` |
| InspectorResourceContainer | `third_party/blink/renderer/core/inspector/inspector_resource_container.cc` + `.h` |
| InspectorResourceContentLoader | `third_party/blink/renderer/core/inspector/inspector_resource_content_loader.cc` + `.h` |
| InspectorDOMStorageAgent | `third_party/blink/renderer/core/inspector/inspector_dom_storage_agent.cc` + `.h` (not in slice) |
| InspectorIndexedDBAgent | `third_party/blink/renderer/core/inspector/inspector_indexed_db_agent.cc` + `.h` (not in slice) |
| InspectorCacheStorageAgent | `third_party/blink/renderer/core/inspector/inspector_cache_storage_agent.cc` + `.h` (not in slice) |
| StorageHandler (browser) | `browser/devtools/protocol/storage_handler.cc` + `.h` |
| DOMHandler (browser, for setFileInputFiles) | `browser/devtools/protocol/dom_handler.cc` + `.h` |
| Protocol definitions | `third_party/blink/public/devtools_protocol/domains/{DOM,CSS,DOMStorage,IndexedDB,CacheStorage,Storage}.pdl` |
| Blink agent config | `third_party/blink/renderer/core/inspector/inspector_protocol_config.json` |

---

## 4.10 Class Diagram

```
                          ┌────────────────────────────┐
                          │   InspectorBaseAgent       │
                          │   <DomainMetainfo>         │
                          │   (inspector_base_agent.h) │
                          └─────────────┬──────────────┘
                                        │
            ┌───────────────────────────┼───────────────────────────┐
            ▼                           ▼                           ▼
┌───────────────────────┐  ┌─────────────────────────┐  ┌─────────────────────────┐
│ InspectorDOMAgent     │  │ InspectorCSSAgent       │  │ InspectorDOMSnapshotAgent│
│ (inspector_dom_agent) │  │ (inspector_css_agent)   │  │ (inspector_dom_snapshot) │
│                       │  │                         │  │                         │
│ - document_           │  │ - dom_agent_            │  │ - dom_agent_            │
│ - id_to_node_         │  │ - inspector_style_sheet │  │                         │
│ - history_            │  │   _for_inline_styles_   │  │                         │
│ - dom_editor_         │  │ - inspector_style_sheet │  │                         │
│                       │  │   _map_                  │  │                         │
│ + getDocument()       │  │ + getComputedStyleFor   │  │ + captureSnapshot()    │
│ + querySelector()     │  │   Node()                │  │                         │
│ + setAttributeValue() │  │ + getMatchedStylesFor   │  │                         │
│ + removeNode()        │  │   Node()                │  │                         │
│ + setFileInputFiles() │  │ + setStyleTexts()       │  │                         │
│ + resolveNode()      │  │ + createStyleSheet()    │  │                         │
│                       │  │ + addRule()             │  │                         │
│ // Instrumentation:    │  │                         │  │                         │
│ DidInsertDOMNode()    │  │                         │  │                         │
│ WillRemoveDOMNode()   │  │                         │  │                         │
│ DidModifyDOMAttr()    │  │                         │  │                         │
│ DidPushShadowRoot()   │  │                         │  │                         │
└───────────┬───────────┘  └─────────────────────────┘  └─────────────────────────┘
            │
            │ uses
            ▼
┌───────────────────────┐         ┌──────────────────────────┐
│ DOMEditor             │◀────────│ InspectorHistory         │
│ (dom_editor.cc)       │         │ (inspector_history.cc)   │
│                       │         │                          │
│ + InsertBefore()      │         │ + Perform()              │
│ + RemoveChild()      │         │ + Undo()                  │
│ + SetAttribute()      │         │ + Redo()                  │
│ + SetOuterHTML()      │         │                          │
└───────────────────────┘         └──────────────────────────┘

                  ┌────────────────────────────────────┐
                  │     Storage Subagents (Blink)      │
                  ├────────────────────────────────────┤
                  │                                    │
                  │ ┌────────────────────────────────┐ │
                  │ │ InspectorDOMStorageAgent      │ │
                  │ │ (not in this slice)            │ │
                  │ │                                │ │
                  │ │ - storage_areas_observed_      │ │
                  │ │ + enable()                     │ │
                  │ │ + getDOMStorageItems()         │ │
                  │ │ + setDOMStorageItem()          │ │
                  │ │ + removeDOMStorageItem()       │ │
                  │ │ + clear()                      │ │
                  │ └────────────────────────────────┘ │
                  │                                    │
                  │ ┌────────────────────────────────┐ │
                  │ │ InspectorIndexedDBAgent        │ │
                  │ │ (not in this slice)            │ │
                  │ │                                │ │
                  │ │ + requestDatabaseNames()       │ │
                  │ │ + requestDatabase()            │ │
                  │ │ + requestData()               │ │
                  │ │ + clearObjectStore()           │ │
                  │ │ + deleteDatabase()              │ │
                  │ └────────────────────────────────┘ │
                  │                                    │
                  │ ┌────────────────────────────────┐ │
                  │ │ InspectorCacheStorageAgent     │ │
                  │ │ (not in this slice)            │ │
                  │ │                                │ │
                  │ │ + requestCacheNames()           │ │
                  │ │ + requestEntries()              │ │
                  │ │ + deleteCache()                 │ │
                  │ │ + requestCachedResponse()       │ │
                  │ └────────────────────────────────┘ │
                  └────────────────────────────────────┘

                  ┌────────────────────────────────────┐
                  │  StorageHandler (browser process)  │
                  │  (storage_handler.cc)               │
                  │                                    │
                  │ - cookies_observed_                  │
                  │ - indexed_db_observers_             │
                  │ - cache_storage_observers_          │
                  │                                    │
                  │ + getCookies()                      │
                  │ + setCookies()                      │
                  │ + clearCookies()                    │
                  │ + trackIndexedDBForOrigin()         │
                  │ + trackCacheStorageForOrigin()      │
                  │ + getStorageKeyForFrame()           │
                  │                                    │
                  │ // Emits change events:              │
                  │ // Storage.indexedDBListUpdated      │
                  │ // Storage.indexedDBContentUpdated    │
                  │ // Storage.cacheStorageListUpdated   │
                  │ // Storage.cacheStorageContentUpdated │
                  └────────────────────────────────────┘
```

---

## 4.11 CDP Command & Event Reference

### 4.11.1 DOM Domain Commands

| Command | Implementation | What it does |
|---|---|---|
| `DOM.enable` | `:739` | Enable DOM agent with optional whitespace filter |
| `DOM.getDocument` | `:767` | Get root document (depth, pierce) |
| `DOM.requestChildNodes` | `:974` | Request children of a node (lazy) |
| `DOM.querySelector` | `:992` | querySelector on a node |
| `DOM.querySelectorAll` | `:1015` | querySelectorAll on a node |
| `DOM.getNodeForLocation` | `:3186` | Hit-test (x, y) → nodeId |
| `DOM.describeNode` | `:3281` | Get Node without binding (no nodeId) |
| `DOM.resolveNode` | `:1908` | nodeId → RemoteObject |
| `DOM.setAttributeValue` | `:1123` | Set single attribute |
| `DOM.setAttributesAsText` | `:1167` | Parse text and apply all attributes |
| `DOM.removeAttribute` | `:1216` | Remove attribute |
| `DOM.removeNode` | `:1229` | Remove node from parent |
| `DOM.setNodeName` | `:1242` | Rename node (creates new element) |
| `DOM.setNodeValue` | `:1371` | Set text content of text/PI node |
| `DOM.getOuterHTML` | `:1310` | Get outer HTML |
| `DOM.setOuterHTML` | `:1331` | Set outer HTML (diff-based patch) |
| `DOM.insertBefore` | `:1271` | Insert node before sibling |
| `DOM.moveNode` | `:1290` | Move node to new parent |
| `DOM.copyTo` | `:1259` | Copy node to new parent |
| `DOM.focus` | `:1771` | Focus element (triggers focus event) |
| `DOM.scrollIntoViewIfNeeded` | `:3300` | Scroll into view if not visible |
| `DOM.setFileInputFiles` | `:1789` | Set files on input type=file |
| `DOM.getFrameOwner` | `:3343` | Find iframe element by frameId |
| `DOM.getFileInfo` | `:3387` | Get file info for input type=file |
| `DOM.performSearch` | (search) | Search DOM by string |
| `DOM.getSearchResults` | (search) | Get paginated search results |
| `DOM.discardSearchResults` | (search) | Discard search |
| `DOM.collectClassNamesFromSubtree` | `:3344` | Get all CSS class names in subtree |
| `DOM.getTopLayerElements` | `:3411` | Get top-layer elements (dialog, popover) |
| `DOM.getAnchorElement` | `:3434` | Get anchor element for target |
| `DOM.getDetachedDomNodes` | `:3466` | Get detached DOM nodes (leak detection) |

### 4.11.2 CSS Domain Commands

| Command | Implementation | What it does |
|---|---|---|
| `CSS.enable` | `:916` | Enable CSS agent (async, waits for stylesheet loads) |
| `CSS.disable` | (reverse) | Disable CSS agent |
| `CSS.getComputedStyleForNode` | `:2397` | Get computed style (all properties + variables) |
| `CSS.getMatchedStylesForNode` | `:1461` | Get full cascade (inline, attributes, matched rules, inherited, pseudos) |
| `CSS.getInlineStylesForNode` | (in CSSAgent) | Get inline `style` attribute + attributes style |
| `CSS.setStyleSheetText` | (in CSSAgent) | Set entire stylesheet source |
| `CSS.getStyleSheetText` | (in CSSAgent) | Get entire stylesheet source |
| `CSS.setStyleTexts` | `:3081` | Batch edit styles (atomic with rollback) |
| `CSS.addRule` | `:3384` | Add a CSS rule to a stylesheet |
| `CSS.createStyleSheet` | `:3360` | Create a new `<style>` element in a frame |
| `CSS.forcePseudoState` | (in CSSAgent) | Force `:hover`, `:active`, etc. for matching |
| `CSS.getBackgroundColors` | (in CSSAgent) | Get background colors for a node |
| `CSS.getLayersForNode` | (in CSSAgent) | Get CSS cascade layers |
| `CSS.trackComputedStyleUpdates` | (in CSSAgent) | Track changes to computed styles |
| `CSS.takeComputedStyleUpdates` | (in CSSAgent) | Take tracked computed style changes |

### 4.11.3 DOMStorage Domain Commands & Events

| Command/Event | What it does |
|---|---|
| `DOMStorage.enable` | Enable DOMStorage agent |
| `DOMStorage.disable` | Disable DOMStorage agent |
| `DOMStorage.getDOMStorageItems` | Get all key/value pairs for a StorageId |
| `DOMStorage.setDOMStorageItem` | Set a key/value pair |
| `DOMStorage.removeDOMStorageItem` | Remove a key |
| `DOMStorage.clear` | Clear all items for a StorageId |
| **Event**: `DOMStorage.domStorageItemAdded` | New key added (storageId, key, newValue) |
| **Event**: `DOMStorage.domStorageItemRemoved` | Key removed (storageId, key) |
| **Event**: `DOMStorage.domStorageItemUpdated` | Value changed (storageId, key, oldValue, newValue) |
| **Event**: `DOMStorage.domStorageItemsCleared` | All items cleared (storageId) |

### 4.11.4 IndexedDB Domain Commands

| Command | What it does |
|---|---|
| `IndexedDB.enable` | Enable IndexedDB agent |
| `IndexedDB.disable` | Disable IndexedDB agent |
| `IndexedDB.requestDatabaseNames` | List all databases for an origin/storageKey/bucket |
| `IndexedDB.requestDatabase` | Get database schema (object stores + indexes) |
| `IndexedDB.requestData` | Page through object store data (with KeyRange, skipCount, pageSize) |
| `IndexedDB.getMetadata` | Get entriesCount + keyGeneratorValue |
| `IndexedDB.clearObjectStore` | Clear all entries in an object store |
| `IndexedDB.deleteObjectStoreEntries` | Delete entries matching a KeyRange |
| `IndexedDB.deleteDatabase` | Delete an entire database |

### 4.11.5 CacheStorage Domain Commands

| Command | What it does |
|---|---|
| `CacheStorage.requestCacheNames` | List all caches for an origin/storageKey/bucket |
| `CacheStorage.requestEntries` | List entries in a cache (with pathFilter, pagination) |
| `CacheStorage.deleteCache` | Delete an entire cache |
| `CacheStorage.deleteEntry` | Delete a single entry from a cache |
| `CacheStorage.requestCachedResponse` | Get the response body for a cached entry (base64) |

### 4.11.6 Storage Domain Commands (Browser-Side)

| Command | What it does |
|---|---|
| `Storage.trackCookies` | Subscribe to cookie changes for an origin |
| `Storage.untrackCookies` | Unsubscribe from cookie changes |
| `Storage.trackIndexedDBForOrigin` | Subscribe to IndexedDB list/content changes |
| `Storage.untrackIndexedDBForOrigin` | Unsubscribe |
| `Storage.trackCacheStorageForOrigin` | Subscribe to CacheStorage changes |
| `Storage.untrackCacheStorageForOrigin` | Unsubscribe |
| `Storage.getStorageKeyForFrame` | Get the storage key for a frame (partition-aware) |
| `Storage.clearDataForOrigin` | Clear all storage for an origin (cookies, localStorage, IDB, CacheStorage, etc.) |
| `Storage.getCookies` | Get all cookies (no filtering) |
| `Storage.setCookies` | Set multiple cookies |
| `Storage.clearCookies` | Clear all cookies |
| `Storage.getStorageBucket` | Get the storage bucket info |
| `Storage.setStorageBucketInfo` | Set storage bucket properties (e.g. expiration) |
| `Storage.setStorageTracking` | Configure storage tracking |

---

## 4.12 Qt6 WebEngine C++ Implementation

### 4.12.1 The DomMonitor Class

Here is a complete, production-ready Qt6 DOM and storage monitoring implementation:

#### `DomMonitor.h`

```cpp
#pragma once

#include <QObject>
#include <QWebSocket>
#include <QHash>
#include <QSet>
#include <QJsonObject>
#include <QJsonArray>
#include <QJsonDocument>
#include <functional>
#include <memory>
#include <optional>

// === DOM data structures ===

struct DomNode {
    int nodeId = 0;
    int parentId = 0;
    int backendNodeId = 0;
    int nodeType = 0;                  // 1=Element, 3=Text, 8=Comment, 9=Document, 10=Doctype, 11=Fragment
    QString nodeName;
    QString localName;
    QString nodeValue;
    QStringList attributes;            // flat: [name1, value1, name2, value2, ...]
    QList<DomNode> children;
    int childNodeCount = 0;
    
    // Optional fields
    QString frameId;
    std::optional<DomNode> contentDocument;
    QList<DomNode> shadowRoots;
    QString shadowRootType;            // "open" | "closed" | "user-agent"
    QList<DomNode> pseudoElements;
    QString pseudoType;
    QString documentURL;
    QString baseURL;
    
    // Convenience accessors
    QString attribute(const QString& name) const {
        for (int i = 0; i < attributes.size() - 1; i += 2) {
            if (attributes[i].compare(name, Qt::CaseInsensitive) == 0) {
                return attributes[i + 1];
            }
        }
        return QString();
    }
    bool isElement() const { return nodeType == 1; }
    bool isText() const { return nodeType == 3; }
    bool isDocument() const { return nodeType == 9; }
};

struct BoxModel {
    QList<double> content;             // 8 doubles: [x1,y1, x2,y2, x3,y3, x4,y4]
    QList<double> padding;
    QList<double> border;
    QList<double> margin;
    int width = 0;
    int height = 0;
};

struct ComputedStyleProperty {
    QString name;
    QString value;
};

struct MatchedRule {
    QString selectorList;              // CSS selector text
    QString origin;                     // "user-agent" | "user" | "regular" | "inspector"
    QString styleSheetId;
    QJsonObject style;                 // CSSStyle object
};

struct MatchedStyles {
    QJsonObject inlineStyle;
    QJsonObject attributesStyle;
    QList<MatchedRule> matchedCSSRules;
    QList<MatchedRule> inheritedEntries;
    QList<QJsonObject> pseudoIdMatches;
    QList<QJsonObject> cssKeyframesRules;
};

struct StorageItem {
    QString key;
    QString value;
};

struct IndexedDBDatabase {
    QString name;
    qint64 version = 0;
    QList<QJsonObject> objectStores;   // each has name, keyPath, autoIncrement, indexes
};

struct IndexedDBEntry {
    QJsonObject key;
    QJsonObject primaryKey;
    QJsonObject value;
};

struct CacheEntry {
    QString requestURL;
    QString requestMethod;
    QList<QPair<QString, QString>> requestHeaders;
    double responseTime = 0;
    int responseStatus = 0;
    QString responseStatusText;
    QString responseType;              // basic | cors | default | error | opaqueResponse | opaqueRedirect
    QList<QPair<QString, QString>> responseHeaders;
};

class DomMonitor : public QObject {
    Q_OBJECT
public:
    explicit DomMonitor(const QUrl& devtoolsUrl, QObject* parent = nullptr);
    ~DomMonitor();
    
    // === DOM domain ===
    void enableDom(bool includeWhitespace = false);
    void disableDom();
    
    void getDocument(int depth = 2, bool pierce = false,
                    std::function<void(const DomNode&)> callback = {});
    
    void requestChildNodes(int nodeId, int depth = 1, bool pierce = false);
    
    void querySelector(int nodeId, const QString& selector,
                      std::function<void(int elementNodeId)> callback);
    void querySelectorAll(int nodeId, const QString& selector,
                         std::function<void(const QList<int>&)> callback);
    
    void getOuterHTML(int nodeId, std::function<void(const QString&)> callback);
    void setOuterHTML(int nodeId, const QString& html);
    
    void setAttributeValue(int nodeId, const QString& name, const QString& value);
    void setAttributesAsText(int nodeId, const QString& text);
    void removeAttribute(int nodeId, const QString& name);
    
    void removeNode(int nodeId);
    void insertBefore(int nodeId, int targetNodeId, int previousNodeId = 0);
    void moveNode(int nodeId, int targetNodeId, int previousNodeId = 0);
    
    void focus(int nodeId);
    void scrollIntoViewIfNeeded(int nodeId);
    
    void resolveNode(int nodeId, const QString& objectGroup,
                    std::function<void(const QJsonObject&)> callback);
    void describeNode(int nodeId, int depth = 0, bool pierce = false,
                     std::function<void(const DomNode&)> callback);
    
    void getNodeForLocation(int x, int y, bool includeUserAgentShadowDOM = false,
                           std::function<void(int nodeId, int backendNodeId,
                                            const QString& frameId)> callback);
    
    void getBoxModel(int nodeId, std::function<void(const BoxModel&)> callback);
    
    // File upload
    void setFileInputFiles(int nodeId, const QStringList& filePaths);
    void setFileInputFiles(const QStringList& filePaths, const QString& selector);
    
    // === CSS domain ===
    void enableCss();
    void disableCss();
    
    void getComputedStyleForNode(int nodeId,
                                std::function<void(const QList<ComputedStyleProperty>&)> callback);
    
    void getMatchedStylesForNode(int nodeId,
                                std::function<void(const MatchedStyles&)> callback);
    
    void setStyleTexts(const QList<QPair<QString, QString>>& edits,
                      std::function<void(const QList<QJsonObject>&)> callback);
    
    QString createStyleSheet(const QString& frameId);
    void addRule(const QString& styleSheetId, const QString& ruleText);
    
    // === DOMStorage domain ===
    void enableDomStorage();
    void disableDomStorage();
    
    void getDomStorageItems(const QString& securityOrigin, bool isLocalStorage,
                           std::function<void(const QList<StorageItem>&)> callback);
    void setDomStorageItem(const QString& securityOrigin, bool isLocalStorage,
                          const QString& key, const QString& value);
    void removeDomStorageItem(const QString& securityOrigin, bool isLocalStorage,
                              const QString& key);
    void clearDomStorage(const QString& securityOrigin, bool isLocalStorage);
    
    // === IndexedDB domain ===
    void enableIndexedDB();
    void disableIndexedDB();
    
    void requestDatabaseNames(const QString& securityOrigin,
                             std::function<void(const QStringList&)> callback);
    
    void requestDatabase(const QString& securityOrigin, const QString& databaseName,
                        std::function<void(const IndexedDBDatabase&)> callback);
    
    void requestData(const QString& securityOrigin, const QString& databaseName,
                    const QString& objectStoreName, int skipCount, int pageSize,
                    std::function<void(const QList<IndexedDBEntry>&, bool hasMore)> callback);
    
    void clearObjectStore(const QString& securityOrigin, const QString& databaseName,
                         const QString& objectStoreName);
    void deleteDatabase(const QString& securityOrigin, const QString& databaseName);
    
    // === CacheStorage domain ===
    void requestCacheNames(const QString& securityOrigin,
                          std::function<void(const QList<QJsonObject>&)> callback);
    
    void requestEntries(const QString& cacheId, int skipCount, int pageSize,
                       const QString& pathFilter,
                       std::function<void(const QList<CacheEntry>&, int returnCount)> callback);
    
    void deleteCache(const QString& cacheId);
    void deleteEntry(const QString& cacheId, const QString& requestUrl);
    
    void requestCachedResponse(const QString& cacheId, const QString& requestUrl,
                              std::function<void(const QByteArray&)> callback);
    
    // === Storage tracking (browser-side) ===
    void trackIndexedDBForOrigin(const QString& origin);
    void untrackIndexedDBForOrigin(const QString& origin);
    void trackCacheStorageForOrigin(const QString& origin);
    void untrackCacheStorageForOrigin(const QString& origin);
    
    void getStorageKeyForFrame(const QString& frameId,
                              std::function<void(const QString&)> callback);
    
    void clearDataForOrigin(const QString& origin, const QStringList& storageTypes);
    
signals:
    // DOM mutation events
    void setChildNodes(int parentId, const QList<DomNode>& nodes);
    void attributeModified(int nodeId, const QString& name, const QString& value);
    void attributeRemoved(int nodeId, const QString& name);
    void characterDataModified(int nodeId, const QString& newValue);
    void childNodeCountUpdated(int nodeId, int count);
    void childNodeInserted(int parentNodeId, int previousNodeId, const DomNode& node);
    void childNodeRemoved(int parentNodeId, int nodeId);
    void inlineStyleInvalidated(const QList<int>& nodeIds);
    void shadowRootPushed(int hostId, const DomNode& root);
    void shadowRootPopped(int hostId, int rootId);
    void pseudoElementAdded(int parentId, const DomNode& pseudoElement);
    void pseudoElementRemoved(int parentId, int pseudoElementId);
    void distributedNodesUpdated(int slotId, const QList<DomNode>& distributedNodes);
    void adoptedStyleSheetsModified(int nodeId, const QString& frameId);
    void documentUpdated();
    void topLayerElementsUpdated();
    void scrollableFlagUpdated(int nodeId, bool isScrollable);
    
    // DOMStorage events
    void domStorageItemAdded(const QString& origin, bool isLocalStorage,
                            const QString& key, const QString& newValue);
    void domStorageItemRemoved(const QString& origin, bool isLocalStorage,
                               const QString& key);
    void domStorageItemUpdated(const QString& origin, bool isLocalStorage,
                              const QString& key, const QString& oldValue, const QString& newValue);
    void domStorageItemsCleared(const QString& origin, bool isLocalStorage);
    
    // Storage tracking events (browser-side)
    void indexedDBListUpdated(const QString& origin);
    void indexedDBContentUpdated(const QString& origin, const QString& databaseName);
    void cacheStorageListUpdated(const QString& origin);
    void cacheStorageContentUpdated(const QString& origin, const QString& cacheName);
    
private:
    void sendCommand(const QString& method, const QJsonObject& params,
                    std::function<void(const QJsonObject&)> callback = {});
    void handleMessage(const QString& message);
    
    static DomNode parseNode(const QJsonObject& obj);
    static BoxModel parseBoxModel(const QJsonObject& obj);
    static QList<StorageItem> parseStorageItems(const QJsonArray& arr);
    static QList<CacheEntry> parseCacheEntries(const QJsonArray& arr);
    
    QWebSocket* m_ws;
    int m_nextId = 1;
    QHash<int, std::function<void(const QJsonObject&)>> m_callbacks;
    QString m_sessionId;
    
    // Track node IDs for mutation tracking
    QHash<int, DomNode> m_nodes;          // nodeId → DomNode (our mirror)
    int m_rootNodeId = 0;
};
```

#### `DomMonitor.cpp` (key methods)

```cpp
#include "DomMonitor.h"
#include <QJsonDocument>
#include <QEventLoop>
#include <QTimer>

// === Helpers ===

DomNode DomMonitor::parseNode(const QJsonObject& obj) {
    DomNode node;
    node.nodeId = obj.value("nodeId").toInt();
    node.parentId = obj.value("parentId").toInt(0);
    node.backendNodeId = obj.value("backendNodeId").toInt(0);
    node.nodeType = obj.value("nodeType").toInt();
    node.nodeName = obj.value("nodeName").toString();
    node.localName = obj.value("localName").toString();
    node.nodeValue = obj.value("nodeValue").toString();
    node.childNodeCount = obj.value("childNodeCount").toInt(0);
    
    // Attributes (flat array)
    const QJsonArray attrs = obj.value("attributes").toArray();
    for (const QJsonValue& a : attrs) {
        node.attributes.append(a.toString());
    }
    
    // Children (recursive)
    if (obj.contains("children")) {
        const QJsonArray children = obj.value("children").toArray();
        for (const QJsonValue& c : children) {
            node.children.append(parseNode(c.toObject()));
        }
    }
    
    // Iframe fields
    node.frameId = obj.value("frameId").toString();
    if (obj.contains("contentDocument")) {
        node.contentDocument = parseNode(obj.value("contentDocument").toObject());
    }
    
    // Shadow roots
    if (obj.contains("shadowRoots")) {
        const QJsonArray srs = obj.value("shadowRoots").toArray();
        for (const QJsonValue& sr : srs) {
            node.shadowRoots.append(parseNode(sr.toObject()));
        }
    }
    node.shadowRootType = obj.value("shadowRootType").toString();
    
    // Pseudo elements
    if (obj.contains("pseudoElements")) {
        const QJsonArray pes = obj.value("pseudoElements").toArray();
        for (const QJsonValue& pe : pes) {
            node.pseudoElements.append(parseNode(pe.toObject()));
        }
    }
    node.pseudoType = obj.value("pseudoType").toString();
    
    // Document fields
    node.documentURL = obj.value("documentURL").toString();
    node.baseURL = obj.value("baseURL").toString();
    
    return node;
}

BoxModel DomMonitor::parseBoxModel(const QJsonObject& obj) {
    BoxModel model;
    auto parseQuad = [](const QJsonArray& arr) -> QList<double> {
        QList<double> result;
        for (const QJsonValue& v : arr) result.append(v.toDouble());
        return result;
    };
    if (obj.contains("content")) model.content = parseQuad(obj.value("content").toArray());
    if (obj.contains("padding")) model.padding = parseQuad(obj.value("padding").toArray());
    if (obj.contains("border"))  model.border = parseQuad(obj.value("border").toArray());
    if (obj.contains("margin"))  model.margin = parseQuad(obj.value("margin").toArray());
    model.width = obj.value("width").toInt();
    model.height = obj.value("height").toInt();
    return model;
}

QList<StorageItem> DomMonitor::parseStorageItems(const QJsonArray& arr) {
    QList<StorageItem> items;
    for (const QJsonValue& v : arr) {
        const QJsonArray pair = v.toArray();
        if (pair.size() >= 2) {
            StorageItem item;
            item.key = pair[0].toString();
            item.value = pair[1].toString();
            items.append(item);
        }
    }
    return items;
}

QList<CacheEntry> DomMonitor::parseCacheEntries(const QJsonArray& arr) {
    QList<CacheEntry> entries;
    for (const QJsonValue& v : arr) {
        const QJsonObject o = v.toObject();
        CacheEntry e;
        e.requestURL = o.value("requestURL").toString();
        e.requestMethod = o.value("requestMethod").toString();
        e.responseTime = o.value("responseTime").toDouble();
        e.responseStatus = o.value("responseStatus").toInt();
        e.responseStatusText = o.value("responseStatusText").toString();
        e.responseType = o.value("responseType").toString();
        
        const QJsonArray reqHeaders = o.value("requestHeaders").toArray();
        for (const QJsonValue& h : reqHeaders) {
            const QJsonObject ho = h.toObject();
            e.requestHeaders.append({ho.value("name").toString(), ho.value("value").toString()});
        }
        const QJsonArray respHeaders = o.value("responseHeaders").toArray();
        for (const QJsonValue& h : respHeaders) {
            const QJsonObject ho = h.toObject();
            e.responseHeaders.append({ho.value("name").toString(), ho.value("value").toString()});
        }
        entries.append(e);
    }
    return entries;
}

// === Constructor / Destructor ===

DomMonitor::DomMonitor(const QUrl& devtoolsUrl, QObject* parent)
    : QObject(parent), m_ws(new QWebSocket) {
    
    connect(m_ws, &QWebSocket::textMessageReceived,
            this, &DomMonitor::handleMessage);
    m_ws->open(devtoolsUrl);
}

DomMonitor::~DomMonitor() {
    if (m_ws->isValid()) m_ws->close();
}

// === CDP plumbing ===

void DomMonitor::sendCommand(const QString& method, const QJsonObject& params,
                              std::function<void(const QJsonObject&)> callback) {
    const int id = m_nextId++;
    if (callback) m_callbacks[id] = callback;
    
    QJsonObject msg;
    msg["id"] = id;
    msg["method"] = method;
    if (!params.isEmpty()) msg["params"] = params;
    if (!m_sessionId.isEmpty()) msg["sessionId"] = m_sessionId;
    
    m_ws->sendTextMessage(QString::fromUtf8(
        QJsonDocument(msg).toJson(QJsonDocument::Compact)));
}

void DomMonitor::handleMessage(const QString& message) {
    const auto doc = QJsonDocument::fromJson(message.toUtf8()).object();
    
    // Response to our command
    if (doc.contains("id")) {
        const int id = doc.value("id").toInt();
        auto it = m_callbacks.find(id);
        if (it != m_callbacks.end()) {
            auto cb = it.value();
            m_callbacks.erase(it);
            if (cb) cb(doc.value("result").toObject());
        }
        return;
    }
    
    // Event
    const QString method = doc.value("method").toString();
    const QJsonObject params = doc.value("params").toObject();
    
    // === DOM mutation events ===
    if (method == "DOM.setChildNodes") {
        const int parentId = params.value("parentId").toInt();
        QList<DomNode> nodes;
        const QJsonArray arr = params.value("nodes").toArray();
        for (const QJsonValue& v : arr) {
            nodes.append(parseNode(v.toObject()));
        }
        emit setChildNodes(parentId, nodes);
    }
    else if (method == "DOM.attributeModified") {
        emit attributeModified(params.value("nodeId").toInt(),
                              params.value("name").toString(),
                              params.value("value").toString());
    }
    else if (method == "DOM.attributeRemoved") {
        emit attributeRemoved(params.value("nodeId").toInt(),
                             params.value("name").toString());
    }
    else if (method == "DOM.characterDataModified") {
        emit characterDataModified(params.value("nodeId").toInt(),
                                  params.value("characterData").toString());
    }
    else if (method == "DOM.childNodeCountUpdated") {
        emit childNodeCountUpdated(params.value("nodeId").toInt(),
                                  params.value("childNodeCount").toInt());
    }
    else if (method == "DOM.childNodeInserted") {
        emit childNodeInserted(params.value("parentNodeId").toInt(),
                              params.value("previousNodeId").toInt(),
                              parseNode(params.value("node").toObject()));
    }
    else if (method == "DOM.childNodeRemoved") {
        emit childNodeRemoved(params.value("parentNodeId").toInt(),
                             params.value("nodeId").toInt());
    }
    else if (method == "DOM.inlineStyleInvalidated") {
        QList<int> nodeIds;
        const QJsonArray arr = params.value("nodeIds").toArray();
        for (const QJsonValue& v : arr) nodeIds.append(v.toInt());
        emit inlineStyleInvalidated(nodeIds);
    }
    else if (method == "DOM.shadowRootPushed") {
        emit shadowRootPushed(params.value("hostId").toInt(),
                             parseNode(params.value("root").toObject()));
    }
    else if (method == "DOM.shadowRootPopped") {
        emit shadowRootPopped(params.value("hostId").toInt(),
                             params.value("rootId").toInt());
    }
    else if (method == "DOM.pseudoElementAdded") {
        emit pseudoElementAdded(params.value("parentId").toInt(),
                              parseNode(params.value("pseudoElement").toObject()));
    }
    else if (method == "DOM.pseudoElementRemoved") {
        emit pseudoElementRemoved(params.value("parentId").toInt(),
                                  params.value("pseudoElementId").toInt());
    }
    else if (method == "DOM.distributedNodesUpdated") {
        QList<DomNode> nodes;
        const QJsonArray arr = params.value("distributedNodes").toArray();
        for (const QJsonValue& v : arr) nodes.append(parseNode(v.toObject()));
        emit distributedNodesUpdated(params.value("slotId").toInt(), nodes);
    }
    else if (method == "DOM.adoptedStyleSheetsModified") {
        emit adoptedStyleSheetsModified(params.value("nodeId").toInt(),
                                       params.value("frameId").toString());
    }
    else if (method == "DOM.documentUpdated") {
        m_nodes.clear();
        emit documentUpdated();
    }
    else if (method == "DOM.topLayerElementsUpdated") {
        emit topLayerElementsUpdated();
    }
    else if (method == "DOM.scrollableFlagUpdated") {
        emit scrollableFlagUpdated(params.value("nodeId").toInt(),
                                   params.value("isScrollable").toBool());
    }
    
    // === DOMStorage events ===
    else if (method == "DOMStorage.domStorageItemAdded") {
        const QJsonObject sid = params.value("storageId").toObject();
        emit domStorageItemAdded(sid.value("securityOrigin").toString(),
                                sid.value("isLocalStorage").toBool(),
                                params.value("key").toString(),
                                params.value("newValue").toString());
    }
    else if (method == "DOMStorage.domStorageItemRemoved") {
        const QJsonObject sid = params.value("storageId").toObject();
        emit domStorageItemRemoved(sid.value("securityOrigin").toString(),
                                   sid.value("isLocalStorage").toBool(),
                                   params.value("key").toString());
    }
    else if (method == "DOMStorage.domStorageItemUpdated") {
        const QJsonObject sid = params.value("storageId").toObject();
        emit domStorageItemUpdated(sid.value("securityOrigin").toString(),
                                   sid.value("isLocalStorage").toBool(),
                                   params.value("key").toString(),
                                   params.value("oldValue").toString(),
                                   params.value("newValue").toString());
    }
    else if (method == "DOMStorage.domStorageItemsCleared") {
        const QJsonObject sid = params.value("storageId").toObject();
        emit domStorageItemsCleared(sid.value("securityOrigin").toString(),
                                    sid.value("isLocalStorage").toBool());
    }
    
    // === Storage tracking events (browser-side) ===
    else if (method == "Storage.indexedDBListUpdated") {
        emit indexedDBListUpdated(params.value("origin").toString());
    }
    else if (method == "Storage.indexedDBContentUpdated") {
        emit indexedDBContentUpdated(params.value("origin").toString(),
                                     params.value("databaseName").toString());
    }
    else if (method == "Storage.cacheStorageListUpdated") {
        emit cacheStorageListUpdated(params.value("origin").toString());
    }
    else if (method == "Storage.cacheStorageContentUpdated") {
        emit cacheStorageContentUpdated(params.value("origin").toString(),
                                        params.value("cacheName").toString());
    }
}

// === DOM domain ===

void DomMonitor::enableDom(bool includeWhitespace) {
    QJsonObject params;
    if (includeWhitespace) {
        params["includeWhitespace"] = "all";
    } else {
        params["includeWhitespace"] = "none";
    }
    sendCommand("DOM.enable", params);
}

void DomMonitor::disableDom() {
    sendCommand("DOM.disable", {});
}

void DomMonitor::getDocument(int depth, bool pierce,
                             std::function<void(const DomNode&)> callback) {
    QJsonObject params;
    params["depth"] = depth;
    params["pierce"] = pierce;
    sendCommand("DOM.getDocument", params, [this, callback](const QJsonObject& result) {
        const DomNode root = parseNode(result.value("root").toObject());
        m_rootNodeId = root.nodeId;
        m_nodes[root.nodeId] = root;
        if (callback) callback(root);
    });
}

void DomMonitor::requestChildNodes(int nodeId, int depth, bool pierce) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["depth"] = depth;
    params["pierce"] = pierce;
    sendCommand("DOM.requestChildNodes", params);
}

void DomMonitor::querySelector(int nodeId, const QString& selector,
                                std::function<void(int)> callback) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["selector"] = selector;
    sendCommand("DOM.querySelector", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("nodeId").toInt(0));
    });
}

void DomMonitor::querySelectorAll(int nodeId, const QString& selector,
                                   std::function<void(const QList<int>&)> callback) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["selector"] = selector;
    sendCommand("DOM.querySelectorAll", params, [callback](const QJsonObject& result) {
        QList<int> nodeIds;
        const QJsonArray arr = result.value("nodeIds").toArray();
        for (const QJsonValue& v : arr) nodeIds.append(v.toInt());
        if (callback) callback(nodeIds);
    });
}

void DomMonitor::getOuterHTML(int nodeId, std::function<void(const QString&)> callback) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    sendCommand("DOM.getOuterHTML", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("outerHTML").toString());
    });
}

void DomMonitor::setOuterHTML(int nodeId, const QString& html) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["outerHTML"] = html;
    sendCommand("DOM.setOuterHTML", params);
}

void DomMonitor::setAttributeValue(int nodeId, const QString& name, const QString& value) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["name"] = name;
    params["value"] = value;
    sendCommand("DOM.setAttributeValue", params);
}

void DomMonitor::setAttributesAsText(int nodeId, const QString& text) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["text"] = text;
    sendCommand("DOM.setAttributesAsText", params);
}

void DomMonitor::removeAttribute(int nodeId, const QString& name) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["name"] = name;
    sendCommand("DOM.removeAttribute", params);
}

void DomMonitor::removeNode(int nodeId) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    sendCommand("DOM.removeNode", params);
}

void DomMonitor::insertBefore(int nodeId, int targetNodeId, int previousNodeId) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["targetNodeId"] = targetNodeId;
    params["insertNodeId"] = previousNodeId;
    sendCommand("DOM.insertBefore", params);
}

void DomMonitor::moveNode(int nodeId, int targetNodeId, int previousNodeId) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["targetNodeId"] = targetNodeId;
    params["previousNodeId"] = previousNodeId;
    sendCommand("DOM.moveNode", params);
}

void DomMonitor::focus(int nodeId) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    sendCommand("DOM.focus", params);
}

void DomMonitor::scrollIntoViewIfNeeded(int nodeId) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    sendCommand("DOM.scrollIntoViewIfNeeded", params);
}

void DomMonitor::resolveNode(int nodeId, const QString& objectGroup,
                             std::function<void(const QJsonObject&)> callback) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["objectGroup"] = objectGroup;
    sendCommand("DOM.resolveNode", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("object").toObject());
    });
}

void DomMonitor::describeNode(int nodeId, int depth, bool pierce,
                              std::function<void(const DomNode&)> callback) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    params["depth"] = depth;
    params["pierce"] = pierce;
    sendCommand("DOM.describeNode", params, [callback](const QJsonObject& result) {
        if (callback) callback(parseNode(result.value("node").toObject()));
    });
}

void DomMonitor::getNodeForLocation(int x, int y, bool includeUserAgentShadowDOM,
                                    std::function<void(int, int, const QString&)> callback) {
    QJsonObject params;
    params["x"] = x;
    params["y"] = y;
    params["includeUserAgentShadowDOM"] = includeUserAgentShadowDOM;
    sendCommand("DOM.getNodeForLocation", params, [callback](const QJsonObject& result) {
        if (callback) callback(
            result.value("nodeId").toInt(),
            result.value("backendNodeId").toInt(),
            result.value("frameId").toString()
        );
    });
}

void DomMonitor::getBoxModel(int nodeId, std::function<void(const BoxModel&)> callback) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    sendCommand("DOM.getBoxModel", params, [callback](const QJsonObject& result) {
        if (callback) callback(parseBoxModel(result.value("model").toObject()));
    });
}

// === File upload ===

void DomMonitor::setFileInputFiles(int nodeId, const QStringList& filePaths) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    QJsonArray files;
    for (const QString& f : filePaths) files.append(f);
    params["files"] = files;
    sendCommand("DOM.setFileInputFiles", params);
}

void DomMonitor::setFileInputFiles(const QStringList& filePaths, const QString& selector) {
    // Find the file input by selector first
    querySelector(m_rootNodeId, selector, [this, filePaths](int nodeId) {
        if (nodeId) setFileInputFiles(nodeId, filePaths);
    });
}

// === CSS domain ===

void DomMonitor::enableCss() {
    sendCommand("CSS.enable", {});
}

void DomMonitor::disableCss() {
    sendCommand("CSS.disable", {});
}

void DomMonitor::getComputedStyleForNode(
        int nodeId,
        std::function<void(const QList<ComputedStyleProperty>&)> callback) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    sendCommand("CSS.getComputedStyleForNode", params, [callback](const QJsonObject& result) {
        QList<ComputedStyleProperty> props;
        const QJsonArray arr = result.value("computedStyle").toArray();
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            ComputedStyleProperty p;
            p.name = o.value("name").toString();
            p.value = o.value("value").toString();
            props.append(p);
        }
        if (callback) callback(props);
    });
}

void DomMonitor::getMatchedStylesForNode(
        int nodeId,
        std::function<void(const MatchedStyles&)> callback) {
    QJsonObject params;
    params["nodeId"] = nodeId;
    sendCommand("CSS.getMatchedStylesForNode", params, [callback](const QJsonObject& result) {
        MatchedStyles ms;
        if (result.contains("inlineStyle")) {
            ms.inlineStyle = result.value("inlineStyle").toObject();
        }
        if (result.contains("attributesStyle")) {
            ms.attributesStyle = result.value("attributesStyle").toObject();
        }
        // Parse matched rules, inherited, pseudos, keyframes
        // (full parsing omitted for brevity)
        if (callback) callback(ms);
    });
}

void DomMonitor::setStyleTexts(
        const QList<QPair<QString, QString>>& edits,
        std::function<void(const QList<QJsonObject>&)> callback) {
    QJsonObject params;
    QJsonArray arr;
    for (const auto& edit : edits) {
        QJsonObject e;
        e["styleSheetId"] = edit.first;
        e["text"] = edit.second;
        arr.append(e);
    }
    params["edits"] = arr;
    sendCommand("CSS.setStyleTexts", params, [callback](const QJsonObject& result) {
        QList<QJsonObject> styles;
        const QJsonArray arr = result.value("styles").toArray();
        for (const QJsonValue& v : arr) styles.append(v.toObject());
        if (callback) callback(styles);
    });
}

QString DomMonitor::createStyleSheet(const QString& frameId) {
    QJsonObject params;
    params["frameId"] = frameId;
    QString sheetId;
    sendCommand("CSS.createStyleSheet", params, [&sheetId](const QJsonObject& result) {
        sheetId = result.value("styleSheetId").toString();
    });
    // Note: async — for sync use QEventLoop
    return sheetId;
}

void DomMonitor::addRule(const QString& styleSheetId, const QString& ruleText) {
    QJsonObject params;
    params["styleSheetId"] = styleSheetId;
    params["ruleText"] = ruleText;
    sendCommand("CSS.addRule", params);
}

// === DOMStorage domain ===

void DomMonitor::enableDomStorage() {
    sendCommand("DOMStorage.enable", {});
}

void DomMonitor::disableDomStorage() {
    sendCommand("DOMStorage.disable", {});
}

void DomMonitor::getDomStorageItems(const QString& securityOrigin, bool isLocalStorage,
                                    std::function<void(const QList<StorageItem>&)> callback) {
    QJsonObject storageId;
    storageId["securityOrigin"] = securityOrigin;
    storageId["isLocalStorage"] = isLocalStorage;
    QJsonObject params;
    params["storageId"] = storageId;
    sendCommand("DOMStorage.getDOMStorageItems", params, [callback](const QJsonObject& result) {
        QList<StorageItem> items = parseStorageItems(result.value("entries").toArray());
        if (callback) callback(items);
    });
}

void DomMonitor::setDomStorageItem(const QString& securityOrigin, bool isLocalStorage,
                                   const QString& key, const QString& value) {
    QJsonObject storageId;
    storageId["securityOrigin"] = securityOrigin;
    storageId["isLocalStorage"] = isLocalStorage;
    QJsonObject params;
    params["storageId"] = storageId;
    params["key"] = key;
    params["value"] = value;
    sendCommand("DOMStorage.setDOMStorageItem", params);
}

void DomMonitor::removeDomStorageItem(const QString& securityOrigin, bool isLocalStorage,
                                      const QString& key) {
    QJsonObject storageId;
    storageId["securityOrigin"] = securityOrigin;
    storageId["isLocalStorage"] = isLocalStorage;
    QJsonObject params;
    params["storageId"] = storageId;
    params["key"] = key;
    sendCommand("DOMStorage.removeDOMStorageItem", params);
}

void DomMonitor::clearDomStorage(const QString& securityOrigin, bool isLocalStorage) {
    QJsonObject storageId;
    storageId["securityOrigin"] = securityOrigin;
    storageId["isLocalStorage"] = isLocalStorage;
    QJsonObject params;
    params["storageId"] = storageId;
    sendCommand("DOMStorage.clear", params);
}

// === IndexedDB domain ===

void DomMonitor::enableIndexedDB() {
    sendCommand("IndexedDB.enable", {});
}

void DomMonitor::disableIndexedDB() {
    sendCommand("IndexedDB.disable", {});
}

void DomMonitor::requestDatabaseNames(const QString& securityOrigin,
                                      std::function<void(const QStringList&)> callback) {
    QJsonObject params;
    params["securityOrigin"] = securityOrigin;
    sendCommand("IndexedDB.requestDatabaseNames", params, [callback](const QJsonObject& result) {
        QStringList names;
        const QJsonArray arr = result.value("databaseNames").toArray();
        for (const QJsonValue& v : arr) names.append(v.toString());
        if (callback) callback(names);
    });
}

void DomMonitor::requestDatabase(const QString& securityOrigin, const QString& databaseName,
                                std::function<void(const IndexedDBDatabase&)> callback) {
    QJsonObject params;
    params["securityOrigin"] = securityOrigin;
    params["databaseName"] = databaseName;
    sendCommand("IndexedDB.requestDatabase", params, [callback](const QJsonObject& result) {
        IndexedDBDatabase db;
        const QJsonObject dbObj = result.value("databaseWithObjectStores").toObject();
        db.name = dbObj.value("name").toString();
        db.version = static_cast<qint64>(dbObj.value("version").toDouble());
        const QJsonArray stores = dbObj.value("objectStores").toArray();
        for (const QJsonValue& v : stores) db.objectStores.append(v.toObject());
        if (callback) callback(db);
    });
}

void DomMonitor::requestData(const QString& securityOrigin, const QString& databaseName,
                            const QString& objectStoreName, int skipCount, int pageSize,
                            std::function<void(const QList<IndexedDBEntry>&, bool)> callback) {
    QJsonObject params;
    params["securityOrigin"] = securityOrigin;
    params["databaseName"] = databaseName;
    params["objectStoreName"] = objectStoreName;
    params["indexName"] = "";
    params["skipCount"] = skipCount;
    params["pageSize"] = pageSize;
    sendCommand("IndexedDB.requestData", params, [callback](const QJsonObject& result) {
        QList<IndexedDBEntry> entries;
        const QJsonArray arr = result.value("objectStoreDataEntries").toArray();
        for (const QJsonValue& v : arr) {
            const QJsonObject o = v.toObject();
            IndexedDBEntry e;
            e.key = o.value("key").toObject();
            e.primaryKey = o.value("primaryKey").toObject();
            e.value = o.value("value").toObject();
            entries.append(e);
        }
        const bool hasMore = result.value("hasMore").toBool();
        if (callback) callback(entries, hasMore);
    });
}

void DomMonitor::clearObjectStore(const QString& securityOrigin, const QString& databaseName,
                                  const QString& objectStoreName) {
    QJsonObject params;
    params["securityOrigin"] = securityOrigin;
    params["databaseName"] = databaseName;
    params["objectStoreName"] = objectStoreName;
    sendCommand("IndexedDB.clearObjectStore", params);
}

void DomMonitor::deleteDatabase(const QString& securityOrigin, const QString& databaseName) {
    QJsonObject params;
    params["securityOrigin"] = securityOrigin;
    params["databaseName"] = databaseName;
    sendCommand("IndexedDB.deleteDatabase", params);
}

// === CacheStorage domain ===

void DomMonitor::requestCacheNames(const QString& securityOrigin,
                                  std::function<void(const QList<QJsonObject>&)> callback) {
    QJsonObject params;
    params["securityOrigin"] = securityOrigin;
    sendCommand("CacheStorage.requestCacheNames", params, [callback](const QJsonObject& result) {
        QList<QJsonObject> caches;
        const QJsonArray arr = result.value("caches").toArray();
        for (const QJsonValue& v : arr) caches.append(v.toObject());
        if (callback) callback(caches);
    });
}

void DomMonitor::requestEntries(const QString& cacheId, int skipCount, int pageSize,
                               const QString& pathFilter,
                               std::function<void(const QList<CacheEntry>&, int)> callback) {
    QJsonObject params;
    params["cacheId"] = cacheId;
    params["skipCount"] = skipCount;
    params["pageSize"] = pageSize;
    if (!pathFilter.isEmpty()) params["pathFilter"] = pathFilter;
    sendCommand("CacheStorage.requestEntries", params, [callback](const QJsonObject& result) {
        QList<CacheEntry> entries = parseCacheEntries(result.value("cacheDataEntries").toArray());
        const int returnCount = result.value("returnCount").toInt();
        if (callback) callback(entries, returnCount);
    });
}

void DomMonitor::deleteCache(const QString& cacheId) {
    QJsonObject params;
    params["cacheId"] = cacheId;
    sendCommand("CacheStorage.deleteCache", params);
}

void DomMonitor::deleteEntry(const QString& cacheId, const QString& requestUrl) {
    QJsonObject params;
    params["cacheId"] = cacheId;
    params["request"] = QJsonObject{{"url", requestUrl}};
    sendCommand("CacheStorage.deleteEntry", params);
}

void DomMonitor::requestCachedResponse(const QString& cacheId, const QString& requestUrl,
                                      std::function<void(const QByteArray&)> callback) {
    QJsonObject params;
    params["cacheId"] = cacheId;
    params["requestUrl"] = requestUrl;
    sendCommand("CacheStorage.requestCachedResponse", params, [callback](const QJsonObject& result) {
        const QString body = result.value("response").toObject().value("body").toString();
        if (callback) callback(QByteArray::fromBase64(body.toUtf8()));
    });
}

// === Storage tracking (browser-side) ===

void DomMonitor::trackIndexedDBForOrigin(const QString& origin) {
    QJsonObject params;
    params["origin"] = origin;
    sendCommand("Storage.trackIndexedDBForOrigin", params);
}

void DomMonitor::untrackIndexedDBForOrigin(const QString& origin) {
    QJsonObject params;
    params["origin"] = origin;
    sendCommand("Storage.untrackIndexedDBForOrigin", params);
}

void DomMonitor::trackCacheStorageForOrigin(const QString& origin) {
    QJsonObject params;
    params["origin"] = origin;
    sendCommand("Storage.trackCacheStorageForOrigin", params);
}

void DomMonitor::untrackCacheStorageForOrigin(const QString& origin) {
    QJsonObject params;
    params["origin"] = origin;
    sendCommand("Storage.untrackCacheStorageForOrigin", params);
}

void DomMonitor::getStorageKeyForFrame(const QString& frameId,
                                       std::function<void(const QString&)> callback) {
    QJsonObject params;
    params["frameId"] = frameId;
    sendCommand("Storage.getStorageKeyForFrame", params, [callback](const QJsonObject& result) {
        if (callback) callback(result.value("storageKey").toString());
    });
}

void DomMonitor::clearDataForOrigin(const QString& origin, const QStringList& storageTypes) {
    QJsonObject params;
    params["origin"] = origin;
    params["storageTypes"] = storageTypes.join(",");
    sendCommand("Storage.clearDataForOrigin", params);
}
```

### 4.12.2 Using the DomMonitor

```cpp
// In your scraper:
auto* dom = new DomMonitor(QUrl("ws://127.0.0.1:9222/devtools/page/<id>"));

// Enable DOM + CSS + storage tracking
dom->enableDom(false);  // don't include whitespace
dom->enableCss();
dom->enableDomStorage();
dom->enableIndexedDB();

// Get the full document tree (including iframes and shadow roots)
dom->getDocument(-1, true, [](const DomNode& root) {
    qDebug() << "Root node:" << root.nodeName << "with" 
             << root.children.size() << "children";
});

// Track DOM mutations in real-time
connect(dom, &DomMonitor::childNodeInserted,
        [](int parentId, int prevId, const DomNode& node) {
    qDebug() << "[DOM+] Node inserted:" << node.nodeName 
             << "parent:" << parentId << "prev:" << prevId;
});

connect(dom, &DomMonitor::childNodeRemoved,
        [](int parentId, int nodeId) {
    qDebug() << "[DOM-] Node removed:" << nodeId << "parent:" << parentId;
});

connect(dom, &DomMonitor::attributeModified,
        [](int nodeId, const QString& name, const QString& value) {
    qDebug() << "[DOM~] Attribute:" << name << "=" << value 
             << "on node:" << nodeId;
});

connect(dom, &DomMonitor::shadowRootPushed,
        [](int hostId, const DomNode& root) {
    qDebug() << "[SHADOW+] Root pushed on host:" << hostId
             << "type:" << root.shadowRootType;
});

// Track localStorage changes
connect(dom, &DomMonitor::domStorageItemAdded,
        [](const QString& origin, bool isLocal, const QString& key, const QString& value) {
    qDebug() << "[LS+] " << origin << (isLocal ? "local" : "session")
             << key << "=" << value;
});

connect(dom, &DomMonitor::domStorageItemUpdated,
        [](const QString& origin, bool isLocal, const QString& key,
           const QString& oldVal, const QString& newVal) {
    qDebug() << "[LS~] " << origin << (isLocal ? "local" : "session")
             << key << ":" << oldVal << "→" << newVal;
});

// === Query the DOM ===
dom->querySelector(dom->m_rootNodeId, "h1", [dom](int nodeId) {
    if (nodeId) {
        dom->getOuterHTML(nodeId, [](const QString& html) {
            qDebug() << "H1 outer HTML:" << html;
        });
        
        dom->getComputedStyleForNode(nodeId, [](const QList<ComputedStyleProperty>& styles) {
            for (const auto& p : styles) {
                if (p.name == "color" || p.name == "font-size") {
                    qDebug() << "  " << p.name << ":" << p.value;
                }
            }
        });
    }
});

// === Modify the DOM ===
dom->querySelector(dom->m_rootNodeId, "#my-element", [dom](int nodeId) {
    if (nodeId) {
        dom->setAttributeValue(nodeId, "data-scraped", "true");
        dom->setAttributeValue(nodeId, "class", "highlighted");
        dom->scrollIntoViewIfNeeded(nodeId);
        dom->focus(nodeId);
    }
});

// === Inject a CSS rule ===
dom->createStyleSheet("<mainFrameId>");  // returns styleSheetId
// Then: dom->addRule(sheetId, ".ad-banner { display: none !important; }");

// === Upload a file ===
dom->setFileInputFiles({"/home/user/uploads/cv.pdf"}, "input[type=file]");

// === Get all localStorage for an origin ===
dom->getDomStorageItems("https://example.com", true, [](const QList<StorageItem>& items) {
    for (const StorageItem& item : items) {
        qDebug() << "  " << item.key << "=" << item.value;
    }
});

// === Get all IndexedDB databases ===
dom->requestDatabaseNames("https://example.com", [dom](const QStringList& names) {
    qDebug() << "IndexedDB databases:" << names;
    
    for (const QString& name : names) {
        dom->requestDatabase("https://example.com", name, [](const IndexedDBDatabase& db) {
            qDebug() << "  DB:" << db.name << "v" << db.version;
            for (const QJsonObject& store : db.objectStores) {
                qDebug() << "    Store:" << store.value("name").toString();
            }
        });
    }
});

// === Page through IndexedDB data ===
dom->requestData("https://example.com", "mydb", "users", 0, 100, "",
    [dom](const QList<IndexedDBEntry>& entries, bool hasMore) {
    qDebug() << "Got" << entries.size() << "entries, hasMore:" << hasMore;
    for (const IndexedDBEntry& e : entries) {
        qDebug() << "  key:" << e.key << "value:" << e.value;
    }
    if (hasMore) {
        // Page 2
        dom->requestData("https://example.com", "mydb", "users", 100, 100, "",
            [](const QList<IndexedDBEntry>&, bool) {
            // ...
        });
    }
});

// === Get all CacheStorage entries ===
dom->requestCacheNames("https://example.com", [dom](const QList<QJsonObject>& caches) {
    for (const QJsonObject& cache : caches) {
        const QString cacheId = cache.value("cacheId").toString();
        const QString cacheName = cache.value("cacheName").toString();
        
        dom->requestEntries(cacheId, 0, 100, "",
            [dom, cacheId](const QList<CacheEntry>& entries, int total) {
            qDebug() << "Cache" << cacheId << "has" << total << "entries";
            
            for (const CacheEntry& e : entries) {
                qDebug() << "  " << e.requestMethod << e.requestURL 
                         << "→" << e.responseStatus;
                
                // Download the response body
                dom->requestCachedResponse(cacheId, e.requestURL, [](const QByteArray& body) {
                    qDebug() << "    Body size:" << body.size();
                });
            }
        });
    }
});

// === Track IndexedDB changes ===
dom->trackIndexedDBForOrigin("https://example.com");
connect(dom, &DomMonitor::indexedDBContentUpdated,
        [dom](const QString& origin, const QString& dbName) {
    qDebug() << "IndexedDB content updated:" << dbName;
    // Re-fetch the data
    dom->requestDatabaseNames(origin, [origin, dbName, dom](const QStringList&) {
        dom->requestData(origin, dbName, "...", 0, 100, "", [](...) {});
    });
});

// === Track CacheStorage changes ===
dom->trackCacheStorageForOrigin("https://example.com");
connect(dom, &DomMonitor::cacheStorageContentUpdated,
        [](const QString& origin, const QString& cacheName) {
    qDebug() << "CacheStorage content updated:" << cacheName;
});

// === Clear all storage for an origin ===
dom->clearDataForOrigin("https://example.com", {"cookies", "local_storage", "indexeddb"});
```

---

## 4.13 Edge Cases

### 4.13.1 Closed Shadow Roots

**Closed shadow roots ARE returned to DevTools clients.** The `Open`/`Closed` distinction is a JS-level encapsulation only; the DevTools protocol has backdoor access to both. This is by design — DevTools needs to inspect closed shadow roots for debugging.

| Shadow Root Mode | JS Can Access? | DevTools Can Access? |
|---|---|---|
| `open` | Yes (`element.shadowRoot`) | Yes |
| `closed` | No (`element.shadowRoot` returns `null`) | Yes |
| `user-agent` | No | Yes (read-only — `AssertEditableElement` refuses edits) |

### 4.13.2 Node ID Stability

- `nodeId` is **NOT stable across navigations** — `documentUpdated` event fires, and you must re-call `getDocument` to get fresh nodeIds.
- `backendNodeId` **IS stable across navigations** (used for HAR export and cross-session references). Use `DOM.describeNode` with `backendNodeId` to get the current nodeId for a backend-stable reference.

### 4.13.3 Iframe OOPIF Handling

| Iframe Type | Process | `pierce: true` Behavior |
|---|---|---|
| Same-origin | Same renderer | Recurses into `contentDocument` |
| Cross-origin OOPIF | Separate renderer | Returns `frameId` but `contentDocument` may be null. Must use `Target.attachToTarget` |
| Cross-origin with COEP/COOP | Separate renderer | Same as OOPIF — separate target required |
| `srcdoc` iframe | Same process | Treated as same-origin, recurses |

### 4.13.4 `documentUpdated` Event

Fires on **every navigation** (cross-document). When it fires:
1. All nodeIds in `id_to_node_` are invalidated
2. The frontend must re-call `DOM.getDocument` to get fresh nodeIds
3. `cached_child_count_` is cleared
4. `children_requested_` is cleared

**For scraping**: always listen for `documentUpdated` and re-fetch the document tree. Without this, you'll get `No node with given id found` errors after any navigation.

### 4.13.5 MutationObserver vs CDP Probes

**Chromium does NOT use MutationObserver for DevTools.** It uses baked-in C++ instrumentation hooks ("probes") in the DOM mutation code paths. This means:

- CDP mutations fire BEFORE the page's `MutationObserver` callback (probes are synchronous in the mutation code path)
- CDP mutations fire even for mutations that `MutationObserver` would miss (e.g., attribute changes during parsing)
- CDP mutations don't require the page to have a `MutationObserver` registered

### 4.13.6 `DOM.setAttributesAsText` Parsing

```cpp
// inspector_dom_agent.cc:1167-1180 (excerpt)
auto getParsedElement = [](Element* element, Element* contextElement,
                           const String& text, bool is_html_document) {
  String markup = element->IsSVGElement() ? StrCat({"<svg ", text, "></svg>"})
                  : element->IsMathMLElement() ? StrCat({"<math ", text, "></math>"})
                  : StrCat({"<span ", text, "></span>"});
  DocumentFragment* fragment = element->GetDocument().createDocumentFragment();
  if (is_html_document && contextElement)
    fragment->ParseHTML(markup, contextElement, nullptr, kAllowScriptingContent);
  else
    fragment->ParseXML(markup, contextElement, IGNORE_EXCEPTION);
  return DynamicTo<Element>(fragment->firstChild());
};
```

So `setAttributesAsText("class='foo' data-x='1'")` is parsed as `<span class='foo' data-x='1'></span>`, and the attributes are extracted from the parsed element. SVG elements get `<svg ...>`, MathML gets `<math ...>`.

### 4.13.7 `DOM.setFileInputFiles` Security

```cpp
// browser/devtools/protocol/dom_handler.cc:42-56
Response DOMHandler::SetFileInputFiles(
    std::unique_ptr<protocol::Array<std::string>> files,
    std::optional<DOM::NodeId> node_id,
    std::optional<DOM::BackendNodeId> backend_node_id,
    std::optional<String> in_object_id) {
  if (!allow_file_access_) return Response::ServerError("Not allowed");
  if (host_) {
    for (const std::string& file : *files) {
      ChildProcessSecurityPolicyImpl::GetInstance()->GrantReadFile(
          host_->GetProcess()->GetID(), base::FilePath::FromUTF8Unsafe(file));
    }
  }
  return Response::FallThrough();      // forwards to renderer InspectorDOMAgent
}
```

The browser handler grants the renderer process file-read permission via `ChildProcessSecurityPolicyImpl::GrantReadFile()` BEFORE forwarding to the renderer. If `allow_file_access_` is false (untrusted client), the call fails.

### 4.13.8 CSS Stylesheet Async Loading

`CSS.enable` is **asynchronous** because it first ensures all stylesheet resources have been re-fetched via `InspectorResourceContentLoader`. This means:
- You must wait for the callback before calling `getComputedStyleForNode`
- For pages with many stylesheets, this can take 100-500ms
- If a stylesheet fails to load (404), the enable callback still fires

### 4.13.9 `DOM.performSearch` Limitations

`DOM.performSearch` searches:
- Node names (tag names)
- Attribute values (NOT attribute names)
- Text content of text nodes
- NOT inside shadow roots (unless `includeUserAgentShadowDOM: true`)
- NOT inside iframes (use `pierce: true` or separate targets)

Search results are paginated via `getSearchResults(searchId, fromIndex, toIndex)`.

### 4.13.10 DOMStorage Partitioning

OOPIFs (cross-origin iframes) have their own storage partition. To watch their localStorage:
1. Attach a separate DevTools target via `Target.attachToTarget`
2. Re-do `DOMStorage.enable` + `getDOMStorageItems` on that target
3. Listen for events on that target's session

For modern storage-partitioned scenarios (e.g. with network isolation), pass `storageKey` instead of `securityOrigin` — obtainable via `Storage.getStorageKeyForFrame(frameId)`.

### 4.13.11 IndexedDB Large Values

For IDB values containing blobs/files:
- The agent truncates to avoid OOM
- The `value` RemoteObject may have `subtype: "blob"` or `subtype: "typedarray"`
- To get the actual blob bytes, you must call `Runtime.callFunctionOn` with a function that reads the blob via `FileReader`

### 4.13.12 CacheStorage `pathFilter`

`requestEntries` with `pathFilter`:
- Filters by substring match on `requestURL`
- Does NOT support regex or glob patterns
- Empty string = all entries
- The `returnCount` field returns the TOTAL count (ignoring `pathFilter`)

---

## 4.14 Performance Impact

### 4.14.1 DOM Monitoring Cost

| Operation | Cost |
|---|---|
| `DOM.enable` (cold start) | ~5-10ms (creates DOMEditor + InspectorHistory) |
| `DOM.getDocument` (depth=2) | ~1-2ms |
| `DOM.getDocument` (depth=-1, pierce=true) | ~50-500ms depending on tree size (can be huge for SPA pages) |
| `DOM.requestChildNodes` (lazy expansion) | ~1ms per node |
| `DOM.querySelector` | ~0.5-5ms (depends on selector complexity) |
| `DOM.querySelectorAll` | ~1-20ms (depends on result count) |
| `DOM.setAttributeValue` | ~0.5ms + triggers MutationObserver + style recalc |
| `DOM.removeNode` | ~0.5ms + triggers MutationObserver + layout |
| `DOM.setOuterHTML` | ~5-50ms (diff-based patch via DOMPatchSupport) |
| Mutation event (per mutation) | ~0.01ms for emit; network roundtrip dominates |

### 4.14.2 CSS Monitoring Cost

| Operation | Cost |
|---|---|
| `CSS.enable` (cold start) | ~100-500ms (waits for stylesheet re-fetch) |
| `CSS.getComputedStyleForNode` | ~5-20ms (forces layout + walks all CSS properties) |
| `CSS.getMatchedStylesForNode` | ~10-50ms (full cascade computation) |
| `CSS.setStyleTexts` (batch) | ~5-20ms per edit + style recalc |
| `CSS.createStyleSheet` | ~1ms |
| `CSS.addRule` | ~1ms + style recalc |

### 4.14.3 Storage Monitoring Cost

| Operation | Cost |
|---|---|
| `DOMStorage.enable` | ~1ms |
| `DOMStorage.getDOMStorageItems` | ~1-5ms (depends on item count) |
| `DOMStorage.setDOMStorageItem` | ~0.5ms (sync mojo call) |
| `IndexedDB.requestDatabaseNames` | ~5-20ms (mojo to storage service) |
| `IndexedDB.requestDatabase` | ~5-20ms |
| `IndexedDB.requestData` (100 entries) | ~10-100ms (cursor walk + serialization) |
| `CacheStorage.requestCacheNames` | ~5-20ms |
| `CacheStorage.requestEntries` (100 entries) | ~10-50ms |
| `CacheStorage.requestCachedResponse` | ~5-50ms (depends on body size) |

### 4.14.4 Memory Overhead

| Storage | Memory |
|---|---|
| Per nodeId mapping | ~50-100 bytes (hash entry) |
| Full DOM tree mirror (10,000 nodes) | ~1-2 MB |
| Per mutation event in flight | ~200 bytes - 2 KB |
| CSS computed style (per node) | ~5-10 KB (all properties) |
| DOMStorage items (1000 items) | ~100-500 KB |
| IndexedDB entries (100 entries) | ~10-100 KB (depends on value size) |

### 4.14.5 Optimization Tips for Scraping

1. **Don't call `getDocument(depth=-1, pierce=true)` on every navigation** — it's expensive. Use mutation events to maintain a live mirror.
2. **Use `querySelector` instead of `getDocument` for specific elements** — much cheaper.
3. **Don't enable CSS unless you need it** — `CSS.enable` waits for stylesheet re-fetch.
4. **Batch `setStyleTexts` instead of individual `setStyleText` calls** — atomic and faster.
5. **Use `backendNodeId` for cross-navigation references** — stable, no re-bind needed.
6. **For large IndexedDB dumps, page through with `skipCount` + `pageSize`** — don't try to read all at once.
7. **Use `CacheStorage.requestEntries` with `pathFilter`** to only fetch entries you care about.
8. **Track storage changes instead of polling** — `Storage.trackIndexedDBForOrigin` + `Storage.trackCacheStorageForOrigin` emit change events.
9. **For DOM mutations, filter by node type** — ignore text node changes if you only care about elements.
10. **Use `DOM.scrollIntoViewIfNeeded` before `getBoxModel`** — ensures the element is in the viewport for accurate coordinates.

---

## 4.15 Security & Privacy Impact

### 4.15.1 What CDP DOM/Storage Can Access

A CDP client with `DOM.enable` + `CSS.enable` + `DOMStorage.enable` + `IndexedDB.enable` + `CacheStorage.enable` can:
- Read the **entire DOM tree** including closed shadow roots and cross-origin iframe content (with OOPIF targets)
- Read **all CSS computed styles** for any element
- Read **all CSS rules** including those from cross-origin stylesheets (via `InspectorResourceContentLoader` re-fetch)
- Read **all localStorage and sessionStorage** for any origin
- Read **all IndexedDB databases** including their full data
- Read **all CacheStorage entries** including response bodies
- **Modify the DOM** (set attributes, remove nodes, set HTML)
- **Modify CSS** (set styles, add rules, create stylesheets)
- **Modify storage** (set/remove localStorage items, clear IndexedDB stores)
- **Upload files** to `<input type=file>` via `DOM.setFileInputFiles`
- **Track all DOM mutations** in real-time
- **Track all storage changes** in real-time
- **Force pseudo-states** (`:hover`, `:active`) via `CSS.forcePseudoState`
- **Hit-test** any coordinate via `DOM.getNodeForLocation`

### 4.15.2 Detection of CDP DOM Interference

A sophisticated anti-bot script can detect CDP-based DOM manipulation:

1. **MutationObserver fires for CDP mutations** — `MutationObserver` callbacks fire when CDP modifies the DOM via `DOM.setAttributeValue`, `DOM.removeNode`, etc. The page can detect unexpected mutations.
2. **`DOM.focus` triggers a `focus` event with `isTrusted: false`** — synthetic focus events are detectable via `event.isTrusted`.
3. **`DOM.setFileInputFiles` triggers `change` event with `isTrusted: false`** — file uploads via CDP are detectable.
4. **`CSS.createStyleSheet` adds a `<style>` element to the DOM** — visible via `MutationObserver` on `document.head`.
5. **`CSS.addRule` modifies existing stylesheets** — detectable via `CSSStyleSheet.prototype.insertRule` override.
6. **`DOM.setOuterHTML` replaces nodes** — the new nodes have different identity (`!==` comparison fails).
7. **`DOM.scrollIntoViewIfNeeded` triggers a `scroll` event** — detectable if the page wasn't already scrolling.

### 4.15.3 Stealth Scraping Best Practices for DOM

1. **Don't modify the DOM unless absolutely necessary** — every modification triggers `MutationObserver` on the page.
2. **Use `Runtime.evaluate` with `document.querySelector` + JS manipulation** instead of `DOM.setAttributeValue` — JS mutations go through the normal path and look more natural.
3. **For file uploads, use `DOM.setFileInputFiles` but be aware of `isTrusted: false`** — some sites check this on the `change` event.
4. **For CSS injection, prefer `Page.addScriptToEvaluateOnNewDocument` with `document.head.appendChild(style)` JS** — looks like a normal script injection.
5. **For reading DOM, use `DOM.getDocument` with `depth=-1, pierce=true`** once and maintain a mirror via mutation events — avoids repeated full-tree fetches.
6. **For closed shadow roots, use `pierce: true`** — DevTools can access them even when JS can't.
7. **For cross-origin iframe inspection, use `Target.setAutoAttach({flatten: true})`** to attach to OOPIF targets transparently.

---

## 4.16 Testing

### 4.16.1 Unit Tests

```cpp
#include <QtTest>
#include "DomMonitor.h"

class TestDomMonitor : public QObject {
    Q_OBJECT
private slots:
    void testGetDocument();
    void testQuerySelector();
    void testMutationTracking();
    void testShadowDom();
    void testDomStorage();
    void testIndexedDB();
    void testCacheStorage();
    void testFileUpload();
};

void TestDomMonitor::testGetDocument() {
    DomMonitor dom(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    dom.enableDom();
    
    QSemaphore sem;
    dom.getDocument(-1, true, [&sem](const DomNode& root) {
        QVERIFY(root.isDocument());
        QVERIFY(!root.children.isEmpty());
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 5000));
}

void TestDomMonitor::testQuerySelector() {
    DomMonitor dom(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    dom.enableDom();
    
    QSemaphore sem;
    dom.getDocument(2, false, [&]() { sem.release(); });
    sem.acquire();
    
    dom.querySelector(dom.m_rootNodeId, "body", [&sem](int nodeId) {
        QVERIFY(nodeId > 0);
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 5000));
}

void TestDomMonitor::testMutationTracking() {
    DomMonitor dom(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    dom.enableDom();
    
    QSignalSpy insertSpy(&dom, &DomMonitor::childNodeInserted);
    QSignalSpy removeSpy(&dom, &DomMonitor::childNodeRemoved);
    QSignalSpy attrSpy(&dom, &DomMonitor::attributeModified);
    
    // Trigger mutations via Runtime.evaluate
    // (e.g., document.body.appendChild(document.createElement('div')))
    // ...
    
    QVERIFY(insertSpy.wait(5000));
    QCOMPARE(insertSpy.count(), 1);
}

void TestDomMonitor::testShadowDom() {
    DomMonitor dom(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    dom.enableDom();
    
    QSignalSpy shadowSpy(&dom, &DomMonitor::shadowRootPushed);
    
    // Page calls element.attachShadow({mode: 'closed'})
    // ...
    
    QVERIFY(shadowSpy.wait(5000));
    // Verify we can see the closed shadow root
}

void TestDomMonitor::testDomStorage() {
    DomMonitor dom(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    dom.enableDomStorage();
    
    QSemaphore sem;
    dom.getDomStorageItems("https://example.com", true, [&sem](const QList<StorageItem>& items) {
        QVERIFY(!items.isEmpty());
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 5000));
}

void TestDomMonitor::testIndexedDB() {
    DomMonitor dom(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    dom.enableIndexedDB();
    
    QSemaphore sem;
    dom.requestDatabaseNames("https://example.com", [&sem](const QStringList& names) {
        QVERIFY(!names.isEmpty());
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 10000));
}

void TestDomMonitor::testCacheStorage() {
    DomMonitor dom(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    
    QSemaphore sem;
    dom.requestCacheNames("https://example.com", [&sem](const QList<QJsonObject>& caches) {
        QVERIFY(!caches.isEmpty());
        sem.release();
    });
    QVERIFY(sem.tryAcquire(1, 10000));
}

void TestDomMonitor::testFileUpload() {
    DomMonitor dom(QUrl("ws://127.0.0.1:9222/devtools/page/test"));
    dom.enableDom();
    
    QSemaphore sem;
    dom.getDocument(2, false, [&]() { sem.release(); });
    sem.acquire();
    
    dom.setFileInputFiles({"/tmp/test.txt"}, "input[type=file]");
    
    // Verify the file was set
    // ...
}
```

---

## 4.17 Roadmap: Unique Features That Beat Puppeteer/Playwright

### 4.17.1 "DOM Diff" — Track Changes Between Snapshots

```cpp
class DomDiffer {
public:
    struct Difference {
        enum Type { Inserted, Removed, Modified, Moved };
        Type type;
        int nodeId;
        QString nodeName;
        QString oldValue;
        QString newValue;
    };
    
    QList<Difference> diff(const DomNode& before, const DomNode& after);
    
    // Reconstruct a tree from a list of mutations
    DomNode applyMutations(const DomNode& before, const QList<Difference>& mutations);
};
```

### 4.17.2 "CSS Live Editor" — Real-Time Style Editing

```cpp
class CssLiveEditor {
public:
    // Edit a style and see it applied live
    void editStyle(int nodeId, const QString& property, const QString& value);
    
    // Toggle a style on/off
    void toggleStyle(int nodeId, const QString& property);
    
    // Visualize the cascade for a property
    QList<MatchedRule> cascadeFor(int nodeId, const QString& property);
    
    // Find unused CSS selectors on the page
    QStringList findUnusedSelectors();
};
```

### 4.17.3 "Storage Inspector" — Unified Storage Browser

```cpp
class StorageInspector : public QAbstractItemModel {
public:
    // Tree structure: Origin → Storage Type → Items
    int rowCount(const QModelIndex& parent) const override;
    
    // Tabs: Cookies, localStorage, sessionStorage, IndexedDB, CacheStorage
    void setActiveStorage(StorageType type);
    
    // Search across all storage
    QList<StorageSearchResult> search(const QString& query);
    
    // Export/import all storage for an origin
    void exportOrigin(const QString& origin, const QString& filepath);
    void importOrigin(const QString& filepath, const QString& origin);
};
```

### 4.17.4 "DOM Snapshot" — Full Page Capture

```cpp
class DomSnapshot {
public:
    // Capture the full DOM tree (including shadow roots, iframes) as JSON
    QJsonObject capture();
    
    // Restore from snapshot (replaces entire document)
    void restore(const QJsonObject& snapshot);
    
    // Diff two snapshots
    QList<Difference> diff(const QJsonObject& snapshot1, const QJsonObject& snapshot2);
};
```

### 4.17.5 "Mutation Logger" — Record and Replay DOM Changes

```cpp
class MutationLogger {
public:
    void startRecording();
    void stopRecording();
    
    // Save mutations to a file
    void save(const QString& filepath);
    
    // Replay mutations (with optional speed control)
    void replay(const QString& filepath, double speedMultiplier = 1.0);
    
    // Filter mutations by type, nodeId, attributeName
    QList<Mutation> filter(MutationFilter filter);
};
```

### 4.17.6 "Element Finder" — Visual Element Picker

```cpp
class ElementFinder : public QObject {
public:
    // Enter "pick mode" — hover to highlight, click to select
    void startPickMode();
    
    // Returns the selected element's nodeId
    // Emits: elementHovered(nodeId), elementPicked(nodeId)
    
    // Find elements by visual properties
    QList<int> findByColor(const QString& color);
    QList<int> findBySize(int minWidth, int minHeight);
    QList<int> findByText(const QString& text);
};
```

---

## 4.18 Summary Cheat Sheet

| Operation | CDP Command | Implementation File:Line |
|---|---|---|
| Enable DOM | `DOM.enable` | `inspector_dom_agent.cc:739` |
| Get document | `DOM.getDocument` | `inspector_dom_agent.cc:767` |
| Query selector | `DOM.querySelector` | `inspector_dom_agent.cc:992` |
| Set attribute | `DOM.setAttributeValue` | `inspector_dom_agent.cc:1123` |
| Remove node | `DOM.removeNode` | `inspector_dom_agent.cc:1229` |
| Set outer HTML | `DOM.setOuterHTML` | `inspector_dom_agent.cc:1331` |
| Resolve node → RemoteObject | `DOM.resolveNode` | `inspector_dom_agent.cc:1908` |
| Set file input files | `DOM.setFileInputFiles` | `inspector_dom_agent.cc:1789` + `dom_handler.cc:42` |
| Scroll into view | `DOM.scrollIntoViewIfNeeded` | `inspector_dom_agent.cc:3300` |
| Get box model | `DOM.getBoxModel` | (in CSSAgent) |
| Enable CSS | `CSS.enable` | `inspector_css_agent.cc:916` |
| Get computed style | `CSS.getComputedStyleForNode` | `inspector_css_agent.cc:2397` |
| Get matched styles | `CSS.getMatchedStylesForNode` | `inspector_css_agent.cc:1461` |
| Set style texts | `CSS.setStyleTexts` | `inspector_css_agent.cc:3081` |
| Create stylesheet | `CSS.createStyleSheet` | `inspector_css_agent.cc:3360` |
| Add CSS rule | `CSS.addRule` | `inspector_css_agent.cc:3384` |
| Enable DOMStorage | `DOMStorage.enable` | `inspector_dom_storage_agent.cc` (not in slice) |
| Get storage items | `DOMStorage.getDOMStorageItems` | `inspector_dom_storage_agent.cc` |
| Set storage item | `DOMStorage.setDOMStorageItem` | `inspector_dom_storage_agent.cc` |
| Enable IndexedDB | `IndexedDB.enable` | `inspector_indexed_db_agent.cc` (not in slice) |
| List databases | `IndexedDB.requestDatabaseNames` | `inspector_indexed_db_agent.cc` |
| Read data | `IndexedDB.requestData` | `inspector_indexed_db_agent.cc` |
| List caches | `CacheStorage.requestCacheNames` | `inspector_cache_storage_agent.cc` (not in slice) |
| List cache entries | `CacheStorage.requestEntries` | `inspector_cache_storage_agent.cc` |
| Get cached response | `CacheStorage.requestCachedResponse` | `inspector_cache_storage_agent.cc` |
| Track IDB changes | `Storage.trackIndexedDBForOrigin` | `storage_handler.cc:800` |
| Track Cache changes | `Storage.trackCacheStorageForOrigin` | `storage_handler.cc:601` |
| Get storage key | `Storage.getStorageKeyForFrame` | `storage_handler.cc` |
| Clear all storage | `Storage.clearDataForOrigin` | `storage_handler.cc` |

---

## End of Part 4

This concludes **Part 4: DOM & Storage Monitoring** — approximately 12,000 words covering the complete DOM domain API, mutation capture via C++ probes, shadow DOM traversal (including closed roots), iframe handling, the CSS domain, DOMStorage tracking, IndexedDB monitoring and data extraction, CacheStorage monitoring, full Qt6 C++ implementation, edge cases, performance, security, testing, and unique features.

---

## What's Next?

**Part 5: Page Lifecycle & Screenshots** (your #6 priority) will cover:
- The complete Page lifecycle event sequence (frameNavigated, lifecycleEvent, domContentEventFired, loadEventFired, etc.)
- The `networkIdle` heuristic (≤2 in-flight requests for 5 seconds)
- `Page.captureScreenshot` — PNG/JPEG/WebP encoding, full-page capture, element-level screenshots
- `Page.printToPDF` — PDF generation
- `Page.navigate` and navigation handling
- `Page.addScriptToEvaluateOnNewDocument` (already covered in Part 3)
- Full Qt6 C++ implementation of a `PageController` class
- Edge cases, performance, security, testing
- Unique features (smart waiting, visual diffing, PDF templating, etc.)

