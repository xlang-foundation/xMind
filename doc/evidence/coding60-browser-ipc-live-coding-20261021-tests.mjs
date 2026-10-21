import test from "node:test";
import assert from "node:assert/strict";
import {sumSquares} from "./sum-squares.mjs";
test("empty input is zero",()=>assert.equal(sumSquares([]),0));
test("positive values are squared",()=>assert.equal(sumSquares([2,3]),13));
test("negative values are squared",()=>assert.equal(sumSquares([-2,-3]),13));
test("fractional values are squared",()=>assert.equal(sumSquares([0.5,1.5]),2.5));
