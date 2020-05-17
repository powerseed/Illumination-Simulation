![GitHub Logo](https://github.com/powerseed/Illumination-Simulation/blob/master/Feature5_antialiasing_d.json.png)


# Illumination-Simulation
A project that implements graphic techniques including:
1. Acceleration using octrees.
2. Transformations
3. Improved Quality using Schlick's Approximation for refraction and Oren-Nayar diffuse reflectance model.
4. CSG objects using union, intersection, and difference of other objects.
5. Antialiasing.
6. Area lighting.

# Steps to build (In Visual Studio):
1. Open ```opengl.sln```. 
2. Right click on the project name, then go to ```Properties```. 
3. In the ```Properties``` Pane, go to ```Debugging```, and in this pane there is an input field for ```Command-line arguments```. Add the name of the ```.json``` file you would like to test to this input field. (All ```.json``` files are stored in the path of ```src\scenes```, from ```c.json``` to ```o.json```, which are to describe the different geometries used for testing this project.)
4. Run the program by clicking ```Start``` button in Visual Studio.
<br>

For more detailed instructions about how to run each ```.json``` file, please read ```Report.docx```
