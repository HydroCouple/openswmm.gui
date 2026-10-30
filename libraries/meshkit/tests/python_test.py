# SPDX-License-Identifier: Apache-2.0
import gc
import math
import os
import pathlib
import tempfile
import time
import unittest
import meshkit
import numpy as np

def request():
    return dict(points=np.array([[0.,0.],[20.,0.],[20.,20.],[0.,20.]]),
                paths=[dict(vertices=[0,1,2,3],kind="outer",marker=10,closed=True)],
                options=dict(cells="mixed",maximum_size=4.,minimum_target=.5,
                             minimum_angle_degrees=20.,adjacent_ratio=3.),
                quad_regions=[dict(ring=[[4,4],[16,4],[16,16],[4,16]],cells="required_quads",spacing=3.)])

class MeshKitTests(unittest.TestCase):
    def test_mixed_buffers_and_ownership(self):
        events=[]
        result=meshkit.run(request(),progress=events.append)
        self.assertTrue(result.report["complete"],result.report)
        self.assertTrue(result.report["constraint_audit_passed"])
        self.assertGreater(result.report["triangles"],0)
        self.assertGreater(result.report["quads"],0)
        self.assertTrue(events)
        xyz=np.asarray(result.vertices)
        cells=np.asarray(result.cells)
        types=np.asarray(result.cell_types)
        self.assertEqual(xyz.shape[1],3)
        self.assertEqual(cells.shape[1],4)
        self.assertEqual(set(types),{3,4})
        self.assertFalse(xyz.flags.writeable)
        self.assertFalse(cells.flags.writeable)
        with self.assertRaises(ValueError): xyz[0,0]=999
        before=xyz.copy()
        del result
        gc.collect()
        np.testing.assert_equal(xyz,before)

    def test_strided_inputs_and_shared_ids(self):
        r=request()
        r["points"]=np.array([[0.,0.,99.],[20.,0.,99.],[20.,20.,99.],[0.,20.,99.],[0.,0.,99.]])[:,:2]
        r["point_ids"]=np.array([10,20,30,40,50],dtype=np.int64)
        r["point_markers"]=np.array([1,0,0,0,2],dtype=np.int32)
        result=meshkit.run(r)
        self.assertTrue(result.report["complete"],result.report)
        mapping=np.asarray(result.input_mapping)
        self.assertEqual(mapping[0],mapping[4])
        self.assertEqual(list(result.alias_ids),[50])
        self.assertEqual(list(result.alias_markers),[2])

    def test_native_raster_and_unknown_coverage(self):
        r=request()
        r["options"].update(terrain_tolerance=.01,require_terrain_certification=True)
        y,x=np.mgrid[-4:25.,-4:25.]
        data=dict(heights=(.01*x+.02*y).astype(np.float32),origin=[-4,-4],spacing=[1,1])
        result=meshkit.run(r,raster=data,provider_identity="plane-v1")
        self.assertTrue(result.report["complete"],result.report)
        self.assertEqual(result.report["provider_identity"],"plane-v1")
        self.assertEqual(result.report["terrain_unknown"],0)
        data["heights"][:]=np.nan
        bad=meshkit.run(r,raster=data)
        self.assertFalse(bad.report["complete"])
        self.assertEqual(len(bad.vertices),0)

    def test_native_source_cleanup_with_heights(self):
        prepared=meshkit.prepare_sources([
            dict(id="domain",role="outer",xy=[[0.,0.],[10.,0.],[10.,10.],[0.,10.]]),
            dict(id="bank",xy=[[0.,5.],[5.,5.],[10.,5.]],z=[8.,8.,8.],marker=17,
                 policy=dict(mode="simplify",maximum_deviation=0.))])
        self.assertTrue(prepared["ok"],prepared)
        self.assertEqual(prepared["features"][1]["removed"],1)
        r=meshkit.prepared_request(prepared,options=dict(maximum_size=2.,minimum_angle_degrees=20))
        result=meshkit.run(r)
        self.assertTrue(result.report["complete"],result.report)
        xyz=np.asarray(result.vertices)
        self.assertTrue(np.any(xyz[:,2]==8.))
        self.assertTrue(result.report["constraint_audit_passed"])

    def test_io_and_atomic_no_replace(self):
        result=meshkit.run(request())
        with tempfile.TemporaryDirectory() as folder:
            p=pathlib.Path(folder)/"mesh.mkb"
            self.assertTrue(meshkit.write(p,result)["complete"])
            original=p.read_bytes()
            self.assertFalse(meshkit.write(p,result)["complete"])
            self.assertEqual(p.read_bytes(),original)
            read=meshkit.read(p)
            self.assertTrue(read.report["complete"],read.report)
            np.testing.assert_equal(np.asarray(read.vertices),np.asarray(result.vertices))
            np.testing.assert_equal(np.asarray(read.cells),np.asarray(result.cells))
            self.assertFalse(meshkit.write(pathlib.Path(folder)/"mesh.2dm",result)["complete"])
            lossy=meshkit.write(pathlib.Path(folder)/"mesh.2dm",result,options=dict(allow_attribute_loss=True))
            self.assertTrue(lossy["complete"],lossy)
            self.assertTrue(lossy["losses"])

    def test_metadata_and_reports(self):
        r=request();r["metadata"]={"description":"before\0after"};r["labels"]=[{"id":17,"name":"bank\0full"}]
        result=meshkit.run(r)
        self.assertEqual(result.metadata["description"],"before\0after")
        self.assertEqual(result.labels[0]["name"],"bank\0full")
        self.assertEqual(result.report["resolved_options"]["terrain_policy"]["mode"],"constant")
        self.assertEqual(result.report["resolved_options"]["limits"]["max_cells"],20000000)
        self.assertIn("examples",result.report)
        with self.assertRaises(ValueError):meshkit.read("invalid\0path.mkb")

    def test_cancellation_and_gil(self):
        n=400
        y,x=np.mgrid[:n,:n]
        r=dict(points=np.column_stack([x.ravel(),y.ravel()]).astype(np.float64),
               paths=[dict(vertices=[0,n-1,n*n-1,n*(n-1)],kind="outer",closed=True)],
               options=dict(maximum_size=1.1,minimum_angle_degrees=20))
        job=meshkit.start(r)
        # result(0) returns promptly while the native worker releases the GIL.
        with self.assertRaises(TimeoutError): job.result(0)
        job.cancel()
        result=job.result(20)
        self.assertEqual(result.report["status"],"cancelled")
        self.assertEqual(len(result.vertices),0)
        self.assertTrue(job.done)

    def test_invalid_requests_are_explicit(self):
        r=request();r["options"]["misspelled"]=1
        with self.assertRaises(ValueError):meshkit.run(r)
        r=request();r["point_ids"]=np.array([2**63,2,3,4],dtype=np.uint64)
        with self.assertRaises(ValueError):meshkit.run(r)
        r=request();r["options"]["minimum_edge"]=100
        result=meshkit.run(r)
        self.assertFalse(result.report["complete"])
        self.assertEqual(len(result.cells),0)
        r=request();r["points"]=r["points"].astype('>f8')
        with self.assertRaises(ValueError):meshkit.run(r)

if __name__=="__main__":unittest.main()
