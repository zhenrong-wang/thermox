# Bounded PI control

`control.pi_bounded.normalized` is a transient control component with explicit normalized setpoint
and measurement signal inputs, a bounded control-command output, and one differential integral
state. It is independent of any particular valve, drum, turbine, or thermal system.

For error `e`, proportional gain `K_p`, integral time `T_i`, tracking time `T_t`, bias `b`, and
integral state `I`, the controller evaluates

```text
e = setpoint - measurement
u_raw = b + K_p e + I
u = clamp(u_raw, lower_limit, upper_limit)

dI/dt = K_p/T_i e + (u - u_raw)/T_t
```

The tracking term is back-calculation anti-windup. When the command saturates, it drives the
integral state toward a finite value consistent with the bounded output. When the command is inside
its limits, `u = u_raw` and the controller reduces to ordinary PI action.

All values are dimensionless and normalized. `integral_time` and `tracking_time` use the platform
time-unit registry and must be strictly positive. `lower_limit` must be smaller than `upper_limit`.
The common valve-command defaults are zero and one, but different normalized ranges can be declared.

## Composition

A typical dynamic path is

```text
drum.level_signal -> PI.measurement
setpoint boundary -> PI.setpoint
PI.command -> actuator lag -> valve.command
```

The PI integral and actuator response remain separate differential states. This preserves physical
ownership, allows actuator time constants to vary independently, and makes each block testable or
replaceable through the registry.

The runnable [closed-loop drum feed-control example](closed-loop-drum-control.md) exercises this
composition through the same declaration, service, graph compiler, and transient solver contracts
used by external callers.

## Numerical and modeling limits

The clamp is continuous and piecewise differentiable; its derivative is zero in saturation and one
inside the active range. The fixed sparse DAE pattern includes all setpoint, measurement, command,
and integral dependencies. Exact operation at a limit is assigned to the saturated branch.

This first controller does not include derivative action, setpoint weighting, bumpless manual/auto
transfer, command rate limiting, dead band, noise filtering, or discrete sampling. Those should be
separate calculation models or composable blocks rather than optional switches that obscure this
controller's equations.
