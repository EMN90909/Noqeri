import test from 'node:test';
import assert from 'node:assert/strict';
import {parseProfile,mergeProfiles} from '../Tools/profiling/merge-nqpgo.mjs';
const a='NQPGO1 0123456789abcdef\n@entry 2 4\ncalc 1 8\n';
const b='NQPGO1 0123456789abcdef\ncalc 1 7\n@entry 2 3\n';
test('merge NQPGO1 execution counts and sort deterministically',()=>{
 const result=mergeProfiles([a,b]);
 assert.equal(result,'NQPGO1 0123456789abcdef\n@entry 2 7\ncalc 1 15\n');
 assert.deepEqual(parseProfile(result).rows.get('calc 1'),15n);
 assert.equal(mergeProfiles([b,a]),result);
});
test('reject stale or ambiguous profiles',()=>{
 assert.throws(()=>mergeProfiles([a,b.replace('0123456789abcdef','1111111111111111')]),/fingerprint mismatch/);
 assert.throws(()=>parseProfile(a+'calc 1 1\n'),/Duplicate/);
 assert.throws(()=>parseProfile('NQPGO1 0123456789abcdef\ncalc 1 0\n'),/Malformed/);
 assert.throws(()=>parseProfile('NQPGO1 0123456789abcdef\ncalc 1 18446744073709551616\n'),/overflow/);
 assert.throws(()=>parseProfile('NQPGO1 0123456789abcdef\ncalc 4294967296 1\n'),/overflow/);
 assert.throws(()=>parseProfile('NQPGO1 0123456789abcde\n'),/header/);
});
test('saturate uint64 counters instead of wrapping',()=>{
 const hi='NQPGO1 0123456789abcdef\ncalc 1 18446744073709551615\n';
 const result=mergeProfiles([hi,b]);
 assert.match(result,/calc 1 18446744073709551615/);
});
