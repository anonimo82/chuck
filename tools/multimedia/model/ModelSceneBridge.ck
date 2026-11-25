@import "ModelIO";
@import "ChuGL";
@import "ModelGeometryBridge.ck";
@import "ModelMaterialBridge.ck";

// A scene snapshot with one GGen per source node and one GMesh per attachment.
// Rebind after replacing the model or editing topology.
public class ModelSceneBridge {
    Model3D @ source;
    GGen root;
    GGen @ nodes[0];
    GMesh @ instances[0];
    ModelGeometryBridge @ geometryViews[0];
    ModelMaterialBridge @ appearances[0];
    float trs[10];
    int ready;

    fun void clear() {
        root.detachChildren();
        root.detachParent();
        nodes.clear();
        instances.clear();
        geometryViews.clear();
        appearances.clear();
        null @=> source;
        0 => ready;
    }

    fun void applyTransform(GGen node, float v[]) {
        node.pos(@(v[0],v[1],v[2]));
        node.sca(@(v[7],v[8],v[9]));
        node.rot(@(0,0,0));
        Math.sqrt(v[3]*v[3]+v[4]*v[4]+v[5]*v[5]) => float len;
        if(len > 0.00000001) {
            node.rotateOnLocalAxis(@(v[3]/len,v[4]/len,v[5]/len),
                2.0*Math.atan2(len,v[6]));
        }
    }

    fun int bind(Model3D model) {
        clear();
        if(model == null) return 0;
        model.nodes() => int count;
        if(count < 1 || count > 4096) return 0;
        for(0 => int i; i<count; i++) {
            if(!model.exportNodeTRS(i,trs)) { clear(); return 0; }
            GGen node;
            applyTransform(node,trs);
            model.nodeParent(i) => int parentIndex;
            if(i==0 && parentIndex==-1) node --> root;
            else if(parentIndex>=0 && parentIndex<i) node --> nodes[parentIndex];
            else { clear(); return 0; }
            nodes << node;
            model.nodeMeshes(i) => int attached;
            if(attached<0 || attached>4096-instances.size()) { clear(); return 0; }
            for(0 => int j; j<attached; j++) {
                model.nodeMeshAt(i,j) => int index;
                ModelGeometryBridge view;
                ModelMaterialBridge appearance;
                if(!view.bind(model,index) || !appearance.bind(model,index)) { clear(); return 0; }
                appearance.loadExternalTexture(model,index);
                GMesh mesh(view.geometry,appearance.activeMaterial()) --> node;
                geometryViews << view;
                appearances << appearance;
                instances << mesh;
            }
        }
        model @=> source;
        1 => ready;
        return 1;
    }

    fun int refresh() {
        if(!ready || source==null || source.nodes()!=nodes.size()) return 0;
        // Stage transforms before applying any of them.
        float staged[nodes.size()*10];
        for(0 => int i; i<nodes.size(); i++) {
            if(!source.exportNodeTRS(i,trs)) return 0;
            for(0 => int j; j<10; j++) trs[j] => staged[i*10+j];
        }
        for(0 => int i; i<geometryViews.size(); i++)
            if(!geometryViews[i].refresh()) return 0;
        for(0 => int i; i<nodes.size(); i++) {
            for(0 => int j; j<10; j++) staged[i*10+j] => trs[j];
            applyTransform(nodes[i],trs);
        }
        return 1;
    }
}
