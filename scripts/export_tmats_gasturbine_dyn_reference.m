function export_tmats_gasturbine_dyn_reference(tmats_root, output_csv)
% Export NASA's unmodified T-MATS simple gas-turbine dynamic example.
%
% The caller owns source revision/checksum verification and MEX compilation.
% This function deliberately loads the supplied model without editing it and
% writes stable, full-precision CSV suitable for a separately checksummed
% Thermox validation-series import.

arguments
    tmats_root (1, 1) string
    output_csv (1, 1) string
end

trunk = fullfile(tmats_root, "Trunk");
library = fullfile(trunk, "TMATS_Library");
example = fullfile(trunk, "TMATS_Examples", "Example_GasTurbine_Dyn");
model = "GasTurbine_Dyn_Template";

required_paths = {
    library,
    fullfile(library, "MEX"),
    fullfile(library, "TMATS_Support"),
    fullfile(library, "MATLAB_Scripts"),
    fullfile(library, "MATLAB_Scripts", "Cantera_Enabled"),
    fullfile(trunk, "TMATS_Tools"),
    example,
    fullfile(example, "SimSetup")
};
for index = 1:numel(required_paths)
    assert(isfolder(required_paths{index}), ...
        "Thermox:TMATS:MissingPath", ...
        "Required T-MATS path is missing: %s", required_paths{index});
    addpath(required_paths{index});
end

previous_directory = pwd;
cleanup_directory = onCleanup(@() cd(previous_directory));
cd(example);

% This is the body of NASA's setup function without its final open_system
% GUI action. Keeping every parameter source in the supplied SimSetup folder
% makes the headless export auditable and avoids changing the reference model.
MWS.engName = "engine1";
MWS.top_level = char(example);
MWS = setup_Solve_temp(MWS);
MWS = setup_Inputs(MWS);
MWS = setup_HPC(MWS);
MWS = setup_Shaft(MWS);
MWS = setup_Noz(MWS);
MWS = setup_HPT(MWS);
MWS = setup_Burner(MWS);
MWS = setup_Duct(MWS);
MWS = setup_Inlet(MWS);

% The supplied model resolves its mask parameters and callbacks in MATLAB's
% base workspace, as NASA's interactive setup script does.
assignin("base", "MWS", MWS);
load_system(model);
cleanup_reference = onCleanup(@() close_reference_model(model));
set_param(model, "SimulationCommand", "update");
simulation = sim(model, "StopTime", num2str(MWS.in.SimTime, 17));

time = simulation.tout(:);
assert(numel(time) == 701 && abs(time(1)) < 1.0e-15 && ...
    abs(time(end) - 10.5) < 1.0e-12, ...
    "Thermox:TMATS:UnexpectedTimeGrid", ...
    "NASA example produced an unexpected output time grid");

station_names = {"s0", "s2", "s3", "s4", "s5", "s7"};
station_values = cell(size(station_names));
for station_index = 1:numel(station_names)
    station = simulation.get(station_names{station_index});
    assert(isstruct(station) && isfield(station, "W") && ...
        isfield(station, "Tt") && isfield(station, "Pt"), ...
        "Thermox:TMATS:UnexpectedStationData", ...
        "Station %s does not expose W/Tt/Pt time series", ...
        station_names{station_index});
    assert_time_grid(station.W.Time, time, station_names{station_index});
    assert_time_grid(station.Tt.Time, time, station_names{station_index});
    assert_time_grid(station.Pt.Time, time, station_names{station_index});
    station_values{station_index} = [
        station.W.Data(:), station.Tt.Data(:), station.Pt.Data(:)];
end

shaft_speed = scalar_series(simulation, "Nmech", time);
fuel_flow = scalar_series(simulation, "Wf", time);
net_thrust = scalar_series(simulation, "Fnet", time);
iterations = scalar_series(simulation, "Iterations", time);

output_directory = fileparts(output_csv);
if strlength(output_directory) > 0 && ~isfolder(output_directory)
    mkdir(output_directory);
end
file = fopen(output_csv, "w");
assert(file >= 0, "Thermox:TMATS:OutputOpenFailed", ...
    "Could not open export path: %s", output_csv);
cleanup_file = onCleanup(@() fclose(file));

fprintf(file, ...
    "time_s,shaft_speed_rpm,fuel_flow_lbm_s,net_thrust_lbf,solver_iterations");
for station_index = 1:numel(station_names)
    station = station_names{station_index};
    fprintf(file, ...
        ",%s_mass_flow_lbm_s,%s_total_temperature_degR,%s_total_pressure_psia", ...
        station, station, station);
end
fprintf(file, "\n");

for row = 1:numel(time)
    fprintf(file, "%.17g,%.17g,%.17g,%.17g,%.17g", ...
        time(row), shaft_speed(row), fuel_flow(row), ...
        net_thrust(row), iterations(row));
    for station_index = 1:numel(station_names)
        values = station_values{station_index};
        fprintf(file, ",%.17g,%.17g,%.17g", ...
            values(row, 1), values(row, 2), values(row, 3));
    end
    fprintf(file, "\n");
end

fprintf("TMATS_EXPORT=success\n");
fprintf("TMATS_EXPORT_SAMPLES=%d\n", numel(time));
fprintf("TMATS_EXPORT_FINAL_TIME=%.17g\n", time(end));
fprintf("TMATS_EXPORT_PATH=%s\n", output_csv);
end

function values = scalar_series(simulation, name, expected_time)
series = simulation.get(name);
assert(isa(series, "timeseries"), ...
    "Thermox:TMATS:UnexpectedScalarData", ...
    "Output %s is not a timeseries", name);
assert_time_grid(series.Time, expected_time, name);
values = series.Data(:);
assert(all(isfinite(values)), "Thermox:TMATS:NonFiniteOutput", ...
    "Output %s contains a non-finite sample", name);
end

function assert_time_grid(actual, expected, name)
actual = actual(:);
assert(numel(actual) == numel(expected) && ...
    max(abs(actual - expected)) < 1.0e-12, ...
    "Thermox:TMATS:MisalignedOutput", ...
    "Output %s does not share the reference time grid", name);
end

function close_reference_model(model)
if bdIsLoaded(model)
    close_system(model, 0);
end
evalin("base", "clear MWS");
end
