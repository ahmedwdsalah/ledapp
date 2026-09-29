type Brand<T, B extends string> = T & { readonly __brand: B };

type Radians = Brand<number, "radians">;

type Vec3 = Readonly<{
  x: number;
  y: number;
  z: number;
}>;

type UnitVec3 = Brand<Vec3, "unit-vec3">;

type Quat = Readonly<{
  x: number;
  y: number;
  z: number;
  w: number;
}>;

type UnitQuat = Brand<Quat, "unit-quat">;

export type { Quat, UnitQuat, Vec3, UnitVec3, Radians };
