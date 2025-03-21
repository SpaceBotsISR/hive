%% Changeable Parameters
correction = false;
sigma = 5e-5;
angle_threshold = 45.775;

%% Script
format long;
% Pose of the tracker in the vive frame
syms vAt_x vAt_y vAt_z
syms vPt_x vPt_y vPt_z
% Correction parameters
phase = 0.0;
tilt = 0.0;
gib_phase = 0.0;
gib_mag = 0.0;
curve = 0.0;
%Integration bounds
B = pi/3;
A = -pi/3;

% Results
final_bound_Px = 0;
final_bound_Py = 0;
final_bound_Pz = 0;
final_bound_Ax = 0;
final_bound_Ay = 0;
final_bound_Az = 0;

% sensors = [0.1 0.1 0.0; -0.1 -0.1 0.0; -0.1 0.1 0.0; 0.1 -0.1 0.0; ...
%     0.05 0.05 0.05; -0.05 -0.05 0.05; -0.05 0.05 0.05; 0.05 -0.05 0.05]';
tracker_position = [0.0; 0.0; 1.0];
tracker_angleaxis = [0.0; 0.0; 0.0];
tracker_rotation = eye(3);
lighthouse_position = [0.0; 0.0; 0.0];
lighthouse_rotation = eye(3);

% Pose of the lighthouse in the Vive frame
vRl = lighthouse_rotation;
vPl = lighthouse_position;
% Pose of the vive frame relatively to the lighthouse
lPv = -vRl.' * vPl;
lRv = vRl.';

% Pose of the tracker in the Vive frame
vAt = [vAt_x vAt_y vAt_z].';
vOMEGAt = [0.0 -vAt_z vAt_y; vAt_z 0.0 -vAt_x; -vAt_y vAt_x 0.0];
vRt = eye(3) + vOMEGAt + 1/factorial(2) * vOMEGAt^2 + ...
    1/factorial(3) * vOMEGAt^3 + ...
    1/factorial(4) * vOMEGAt^4 + ...
    1/factorial(5) * vOMEGAt^5 + ...
    1/factorial(6) * vOMEGAt^6 + ...
    1/factorial(7) * vOMEGAt^7 + ...
    1/factorial(8) * vOMEGAt^8 + ...
    1/factorial(9) * vOMEGAt^9;
% syms r11 r12 r13 r21 r22 r23 r31 r32 r33
% vRt = [r11 r12 r13; r21 r22 r23; r31 r32 r33];
vPt = [vPt_x;vPt_y;vPt_z];

% Position of the sensors in the tracker frame
% DIM: [N_sensors, 3]
tPs = sensors;
tNs = normals;

% Full probability
MPalphaH = 1;
MPalphaV = 1;
variables = [];
alphas = [];
for i = 1:size(sensors,2)
    % Frame conversion
    lPs = lRv * (vRt * tPs(:,i) + vPt) + lPv ;
    lNs = lRv * vRt * tNs(:,i);
    
    s_lPs = subs(lPs,[vPt_x vPt_y vPt_z vAt_x vAt_y vAt_z], ...
        [tracker_position' ...
        tracker_angleaxis']);
    s_lNs = subs(lNs,[vPt_x vPt_y vPt_z vAt_x vAt_y vAt_z], ...
        [tracker_position' ...
        tracker_angleaxis']);
 
    if acos(dot(s_lPs/norm(s_lPs), s_lNs)) > angle_threshold * pi/180
       disp([num2str(i) ' out1'])
       continue
    end
    if acos(dot(s_lPs/norm(s_lPs), s_lNs)) < -angle_threshold * pi/180
       disp([num2str(i) ' out2'])
       continue
    end
    disp([num2str(i) ' in'])
   

    x = (lPs(1)/lPs(3));
    y = (lPs(2)/lPs(3));

    if (correction)
        % Horizontal
        alphaH = atan(x) - phase - tan(tilt) * y - curve * y * y - sin(gib_phase + atan(x)) * gib_mag;
        % Vertical
        alphaV = atan(y) - phase - tan(tilt) * x - curve * x * x - sin(gib_phase + atan(y)) * gib_mag;
    else
        % Horizontal
        alphaH = atan(x);
        % Vertical
        alphaV = atan(y);
    end

    % Measurement and measurement noise
    horizontal_string = ['MalphaH' num2str(i)];
    vertical_string = ['MalphaV' num2str(i)];
    MalphaH = sym(horizontal_string);
    MalphaV = sym(vertical_string);
    variables = [variables, MalphaH];
    variables = [variables, MalphaV];
    alphas = [alphas, subs(alphaH,[vPt_x vPt_y vPt_z vAt_x vAt_y vAt_z], ...
        [tracker_position' ...
        tracker_angleaxis'])];
    alphas = [alphas, subs(alphaV,[vPt_x vPt_y vPt_z vAt_x vAt_y vAt_z], ...
        [tracker_position' ...
        tracker_angleaxis'])];    
    % Probability of an horizontal measurement given the parameters
    NalphaH = 1/(sqrt(2*pi))*exp(-0.5 * ((MalphaH - alphaH)/sigma)^2);
%     BalphaH = int(1/(sqrt(2*pi))*exp(-0.5 * ((MalphaH - alphaH)/sigma)^2),MalphaH,0,B);
%     AalphaH = int(1/(sqrt(2*pi))*exp(-0.5 * ((MalphaH - alphaH)/sigma)^2),MalphaH,0,A);
    % Truncated Normal Distribution
    PalphaH = NalphaH / (sigma);% * (BalphaH - AalphaH));
    % log likedlihood
    MPalphaH = MPalphaH * PalphaH;

    % Probability of an vertical measurement given the parameters
    NalphaV = 1/(sqrt(2*pi))*exp(-0.5 * ((MalphaV - alphaV)/sigma)^2);
%     BalphaV = int(1/(sqrt(2*pi))*exp(-0.5 * ((MalphaV - alphaV)/sigma)^2),MalphaV,0,B);
%     AalphaV = int(1/(sqrt(2*pi))*exp(-0.5 * ((MalphaV - alphaV)/sigma)^2),MalphaV,0,A);
    % Truncated Normal Distribution
    PalphaV = NalphaV / (sigma);% * (BalphaV - AalphaV));
    % log likedlihood
    MPalphaV = MPalphaV * PalphaV;


end

% return
MPalpha = MPalphaH * MPalphaV;

theta = vPt_x;
Info_Px = subs(subs(diff(diff(log(MPalpha),theta),theta),[vPt_x vPt_y vPt_z vAt_x vAt_y vAt_z], ...
        [tracker_position' ...
        tracker_angleaxis']),variables,alphas);
% disp(double(-1/subs(subs(diff(diff(log(MPalpha),theta),theta),[vPt_x vPt_y vPt_z vAt_x vAt_y vAt_z], ...
%         [tracker_position' ...
%         tracker_angleaxis']),variables,alphas)));
% for index = 1:length(variables)
% %     disp(variables(index))
%     Info_Px = -int(Info_Px,variables(index),A,B);
% end
Info_Px = -Info_Px;
disp(['V_Px: ' num2str(double(1/Info_Px))]);

theta = vPt_y;
Info_Py = subs(subs(diff(diff(log(MPalpha),theta),theta),[vPt_x vPt_y vPt_z vAt_x vAt_y vAt_z], ...
        [tracker_position' ...
        tracker_angleaxis']),variables, alphas);
% for index = 1:length(variables)
%     Info_Py = -int(Info_Py,variables(index),A,B);
% end
Info_Py = -Info_Py;
disp(['V_Py: ' num2str(double(1/Info_Py))]);

theta = vPt_z;
Info_Pz = subs(subs(diff(diff(log(MPalpha),theta),theta),[vPt_x vPt_y vPt_z vAt_x vAt_y vAt_z], ...
        [tracker_position' ...
        tracker_angleaxis']),variables, alphas);
% for index = 1:length(variables)
%     Info_Pz = -int(Info_Pz,variables(index),A,B);
% end
Info_Pz = -Info_Pz;
disp(['V_Pz: ' num2str(double(1/Info_Pz))]);

theta = vAt_x;
Info_Ax = subs(subs(diff(diff(log(MPalpha),theta),theta),[vPt_x vPt_y vPt_z vAt_x vAt_y vAt_z], ...
        [tracker_position' ...
        tracker_angleaxis']),variables, alphas);
% for index = 1:length(variables)
%     Info_Ax = -int(Info_Ax,variables(index),A,B);
% end
Info_Ax = -Info_Ax;
disp(['V_Ax: ' num2str(double(1/Info_Ax))]);

theta = vAt_y;
Info_Ay = subs(subs(diff(diff(log(MPalpha),theta),theta),[vPt_x vPt_y vPt_z vAt_x vAt_y vAt_z], ...
        [tracker_position' ...
        tracker_angleaxis']),variables, alphas);
% for index = 1:length(variables)
%     Info_Ay = -int(Info_Ay,variables(index),A,B);
% end
Info_Ay = -Info_Ay;
disp(['V_Ay: ' num2str(double(1/Info_Ay))]);

theta = vAt_z;
Info_Az = subs(subs(diff(diff(log(MPalpha),theta),theta),[vPt_x vPt_y vPt_z vAt_x vAt_y vAt_z], ...
        [tracker_position' ...
        tracker_angleaxis']),variables, alphas);
% for index = 1:length(variables)
%     Info_Az = -int(Info_Az,variables(index),A,B);
% end
Info_Az = -Info_Az;
disp(['V_Az: ' num2str(double(1/Info_Az))]);

return